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
stopped, 1.5 seconds more, and the world stopped, whatever a cinematic
left on the screen cleared (its letterbox, titles and the screen's fade);
the pose's structure (always: a map may start in another), its own lines
and objects a frame each, each frame drawn and not shown (a camera put
outside the map is drawn from the leaf it was last drawn in), settled (its
settle_seconds' ticks), the player's body gone;
the camera put at the pose (debug_camera_set, camera_pose.c); and one
frame drawn, each texture it draws with waited for (xbox_texture_cache.c),
with only the pipelines it draws with made (render_enumerate.c's map's
enumeration left out), at the window's size, and kept as it is presented
instead of shown (d3d8_device.c; menu_pictures_render_kept cuts and scales
it). Whatever the player's view settings, the frame is drawn with the
pose's own field of view, no Panini projection, the reticle in the middle
and every line drawn (port_config.c's config_picture_view). Sound is off
meanwhile.

While the pictures are rendered the window shows the loading screen, one
for the whole pass (loading_screen.h: the main menu's ring, the light
crossing it once as the pictures are made, the monks' chant swelling),
with the caption "Preparing menu pictures", "4 of 11"; the maps' own
loading screens are not shown, nor any frame of the game's (the frames
kept are not presented). It fades out over the main menu loading. It
counts only the pictures that can be made (menu_pictures_wanted: none
whose map is missing or modded, nor one that failed twice; with none, no
screen at all). Nothing stops it: a picture that fails is noted and passed
at once, its original kept. Its maps are the game's own, as its loader finds
and takes them (cache_files_map_copyable_checksum: paths.data, the
language's maps folder, any case), and each map's file is copied into the
cache a frame at a time here (prepare_precache), never in the loading's own
wait, which waits for ever and stops the game on a failed copy. A picture
past 30 seconds, or the pass past 120 (menu_picture_overdue), ends the pass:
the main menu shows, the pictures not made keeping their originals. Should
the game stop on a map all the same (the error a damaged disc gives:
menu_pictures_abandon), that picture is given up for good and the error is
shown, not the loading screen.
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
#include "render/render.h"
#include "scenario/scenario.h"
#include "sound/sound_manager.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"

#include "menu_pictures.h"
#include "../src/loading_screen.h"

/* sdl_platform.c's: a map load's loading screen given back */
void halo_loading_end(void);

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
	/* (nanoseconds) the loading screen's fade at the end */
	PREPARE_SCREEN_FADE = 550000000,
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
	done (rendered or failed) */
	long poses[MAXIMUM_MENU_PICTURES];
	long count;
	long index;
	long done, made;
	long frames, picture_frames;
	long ticks, quiet_ticks;
	/* (where in the pose's lines the next is; a frame drawn for one) */
	long line;
	int line_drawn;
	/* the frame drawn this frame (the one kept), and when the loading
	screen began its fade (its progress at 1) */
	int drawn;
	unsigned long long faded;
	/* the frame is kept as it is drawn */
	int capture_wanted, captured;
	unsigned long long began, picture_began, phase_began;
	double phases[NUMBER_OF_PHASES];
	double totals[NUMBER_OF_PHASES];
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

unsigned long menu_picture_map_checksum(
	char const *map)
{
	return cache_files_map_copyable_checksum(map);
}

int menu_pictures_preparing(void)
{
	return prepare.state != _prepare_off;
}

int menu_pictures_frame_drawn(void)
{
	return prepare.state == _prepare_off || prepare.drawn;
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

/* the next picture's map asked for, or the main menu after the last */
static void prepare_next(
	void)
{
	long pose;

	prepare.capture_wanted = FALSE;
	prepare.captured = FALSE;
	if (prepare.index >= prepare.count)
	{
		platform_log("menu pictures: %ld of %ld made%s in %.2f s (load %.2f, opening %.2f, scene %.2f, objects %.2f, "
			"frame %.2f, kept %.2f, after %.2f)", prepare.made, prepare.count, prepare.made < prepare.count ?
			", the rest failed (their originals kept)" : "",
			(double)(prepare_now() - prepare.began) / 1e9, prepare.totals[_phase_load], prepare.totals[_phase_opening],
			prepare.totals[_phase_scene], prepare.totals[_phase_objects], prepare.totals[_phase_frame],
			prepare.totals[_phase_keep], prepare.totals[_phase_after]);
		main_queue_map_name(NULL);
		/* (the loading screen fades out, over the main menu loading) */
		halo_loading_screen_progress(1.0f, NULL);
		prepare.faded = prepare_now();
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
	/* (its map's file copied into the cache first, a frame at a time:
	_prepare_load) */
	prepare.state = _prepare_load;
}

/* a map's file copied into the cache, as the game's background copy does
(main_queue_map_private), but a frame at a time here, where the pass's time
is counted: never the loading's own wait for it (game.c's, which waits for
ever, and gives a failed copy the damaged disc's error, which stops the
game): 1 copied, 0 not yet, -1 it cannot be */
static int prepare_precache(
	char const *scenario)
{
	real progress;

	if (cache_files_precache_map_loaded(scenario))
		return 1;
	if (cache_files_precache_in_progress())
	{
		short status = cache_files_precache_map_status(&progress);
		boolean ours = cache_files_precache_is_copying_map(scenario);

		/* (ended once it has: this map's copied or failed; another's, this
		one's begun at the next frame) */
		if (status == _cached_map_file_in_progress)
			return 0;
		cache_files_precache_map_end();
		if (cache_files_precache_map_loaded(scenario))
			return 1;
		return ours ? -1 : 0;
	}
	if (!cache_files_precache_map_begin(scenario, FALSE))
		return -1;
	return 0;
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
	prepare.index = prepare.done = prepare.made = 0;
	csmemset(prepare.totals, 0, sizeof(prepare.totals));
	for (index = 0; index < prepare.count; index++)
	{
		if (menu_picture_poses[prepare.poses[index]].colour_curve)
			prepare_keep_original(prepare.poses[index]);
	}
	config_picture_view(TRUE);
	sound_stop_all();
	sound_enable(FALSE);
	/* (the loading screen the whole pass: the main menu's ring under the
	light, crossing once as the pictures are made, the monks' chant; the
	main menu's own load's, still up until its first frame is shown, given
	back first) */
	halo_loading_end();
	halo_loading_screen_begin(TRUE);
	prepare.drawn = 0;
	prepare_next();
}

/* (Escape and Start open no pause menu meanwhile) */
static void prepare_input(
	void)
{
	ui_widgets_disable_pause_game(2);
}

static void prepare_update(
	int main_menu_loaded)
{
	struct menu_picture_pose const *picture = &menu_picture_poses[prepare.poses[prepare.index < prepare.count ?
		prepare.index : 0]];
	char line[MAXIMUM_RENDER_LINE];

	/* (the picture's or the pass's time spent: the pass ended, the main menu
	shown, the pictures not made keeping their originals; the one being made
	counted as failed) */
	if (prepare.state != _prepare_menu && prepare.index < prepare.count &&
		menu_picture_overdue(prepare_now(), prepare.began, prepare.picture_began))
	{
		platform_log("menu pictures: out of time: the pass ends, the %ld not made keep their originals",
			prepare.count - prepare.made);
		prepare.capture_wanted = FALSE;
		menu_pictures_render_failed(prepare.poses[prepare.index], "took too long");
		prepare_picture_log(prepare.poses[prepare.index], "failed");
		prepare.index = prepare.count;
		prepare_next();
		return;
	}
	switch (prepare.state)
	{
	case _prepare_load:
		switch (prepare_precache(picture->scenario))
		{
		case 1:
			main_menu_pictures_map(picture->scenario);
			prepare.frames = 0;
			prepare.state = _prepare_loading;
			break;
		case -1:
			prepare_picture_failed("could not be read");
			break;
		default:
			break;
		}
		break;
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
					/* (begun, and followed at its turn: _prepare_load) */
					prepare_precache(scenario);
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
		/* (the structure switched at this frame's start) */
		prepare_command("debug_camera_set \"0 0 0 0 0 0 50\"");
		prepare.line = 0;
		prepare.state = _prepare_objects;
		break;
	case _prepare_objects:
	{
		char const *commands = picture->commands ? picture->commands + prepare.line : NULL;

		/* (the pose's lines a frame each, as it was found with: one may need
		the one before it taken, a camera set by the next frame) */
		if (commands && *commands)
		{
			char const *end = strchr(commands, '\n');
			long length = end ? (long)(end - commands) : (long)strlen(commands);

			prepare.line += end ? length + 1 : length;
			if (length >= MAXIMUM_RENDER_LINE)
				length = MAXIMUM_RENDER_LINE - 1;
			memcpy(line, commands, length);
			line[length] = 0;
			prepare_command(line);
			/* (a frame drawn, not shown, at each: the camera's last leaf in
			the map is where a camera outside it is drawn from, as when the
			pose was found) */
			prepare.line_drawn = TRUE;
			break;
		}
		/* (a vehicle comes to rest on the ground, a rider takes its seat) */
		if (picture->settle_milliseconds > 0)
			prepare_ticks((picture->settle_milliseconds * TICKS_PER_SECOND + 999) / 1000);
		/* (the player's body gone after the pose's own lines, which may
		need it: cheat_teleport_to_camera, for where the camera's view is
		drawn from) */
		prepare_command("object_destroy (unit (list_get (players) 0))");
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
			prepare_next();
		}
		break;
	case _prepare_menu:
		if (main_menu_loaded && prepare_now() - prepare.faded >= PREPARE_SCREEN_FADE)
		{
			halo_loading_screen_end();
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

/* the loading screen's progress (the pictures made, and how far the one
being made has come) and its caption ("4 of 11": the pictures made) */
static void prepare_screen(
	void)
{
	static real const parts[] =
	{
		0.0f, /* _prepare_off */
		0.0f, /* _prepare_load */
		0.0f, /* _prepare_loading */
		0.6f, /* _prepare_opening */
		0.75f, /* _prepare_structure */
		0.85f, /* _prepare_objects */
		0.9f, /* _prepare_pose */
		0.95f, /* _prepare_capture */
		1.0f, /* _prepare_menu */
	};
	char caption[96];

	if (prepare.state == _prepare_menu || prepare.count <= 0)
		return;
	snprintf(caption, sizeof(caption), "Preparing menu pictures \xB7 %ld of %ld", prepare.done, prepare.count);
	halo_loading_screen_progress((prepare.done + parts[prepare.state]) / (real)prepare.count, caption);
}

void menu_pictures_frame(
	int main_menu_loaded)
{
	static int looked;

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
		prepare_input();
		prepare_update(main_menu_loaded);
	}
	if (prepare.state == _prepare_off)
		return;
	/* this frame: the game's kept, or none (the loading screen's own on
	its clock, with its progress and caption) */
	prepare.drawn = prepare.state == _prepare_capture && prepare.capture_wanted && !prepare.captured ? 2 :
		prepare.line_drawn ? 3 : 0;
	prepare.line_drawn = FALSE;
	prepare_screen();
}

void menu_pictures_abandon(
	char const *why)
{
	if (prepare.state == _prepare_off)
		return;
	/* (the picture being made never tried again: the next start shows the
	main menu, not this again) */
	if (prepare.state != _prepare_menu && prepare.index < prepare.count)
		menu_pictures_render_given_up(prepare.poses[prepare.index], why);
	platform_log("menu pictures: the pass stopped (%s)", why);
	main_queue_map_name(NULL);
	prepare.capture_wanted = FALSE;
	prepare.state = _prepare_off;
	prepare.drawn = 0;
	/* (the game's own frames shown again: its error, not the loading screen) */
	halo_loading_screen_end();
	config_picture_view(FALSE);
	sound_enable(TRUE);
	game_time_set_speed(1.0f);
}

int menu_pictures_frame_kept(
	void)
{
	/* (a frame drawn for a pose's line: not shown either) */
	if (prepare.state != _prepare_off && prepare.drawn == 3)
		return TRUE;
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
