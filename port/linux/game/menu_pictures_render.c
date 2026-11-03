/*
MENU_PICTURES_RENDER.C

The game's half of the menus' pictures drawn again from the player's maps
(menu_pictures.h; the platform's half, which decides, starts the copies,
caches and draws, is port/linux/src/menu_pictures.c).

menu_picture_at: which pose's original frame a texture is, wherever the
menus draw it: ui.map's bitmap itself (the multiplayer map's picture), or a
menu file's scaled copy of one of its frames (the campaign's cards:
menu_tags.c's placements).

In a copy started to render a picture (debug.render_picture), each frame
(main.c) takes it a step: in the main menu, the original frame's texels
kept for its colours and the pose's map loaded (map_name); its checksum
checked against the pose's (another is a modded map: noted, and the copy
ends); the map let run until a cinematic is skipped and its first frames
are drawn; then the scene set from the console, a line a frame, as the
pose was found with (tools/preview_rerender/game.py): no console text, HUD,
help text, letterbox or AI, the player's body gone, the world stopped
(game_speed 0); the pose's structure (always: a map may start in another),
its own lines and objects (the world let run a moment for them to settle);
the camera put at the pose (debug_camera_set, camera_pose.c), and frames
drawn there until the textures it sees have loaded; then the frame kept
(menu_pictures_capture, as it is presented) and the copy ends. Anything
that takes too long ends it as failed.
*/

#include "cseries.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmaps.h"
#include "cache/cache_files.h"
#include "cache/texture_cache.h"
#include "cutscene/cinematics.h"
#include "game/game.h"
#include "hs/hs.h"
#include "tag_files/tag_groups.h"

#include "menu_pictures.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

/* ---------- constants */

#define BITMAP_TAG 'bitm'
#define MAXIMUM_RENDER_STEPS 48
#define MAXIMUM_RENDER_LINE 512

/* (milliseconds) the longest the map takes to load; how long it runs
before the scene is set, at least, and its cinematic's longest; how long
the frames are drawn at the pose (its textures loaded); the longest a
frame takes to be kept */
#define RENDER_LOAD_LONGEST 90000
#define RENDER_SETTLE 5000
#define RENDER_CINEMATIC_LONGEST 20000
#define RENDER_POSE_FRAMES 60
#define RENDER_POSE_MILLISECONDS 2500
#define RENDER_CAPTURE_LONGEST 10000

enum render_state
{
	_render_start,
	_render_loading,
	_render_settling,
	_render_steps,
	_render_capturing,
};

enum render_step_kind
{
	_step_command,
	_step_frames,
	_step_milliseconds,
	_step_quiet,
	_step_capture,
};

/* ---------- prototypes */

/* the platform layer's (unseen_frames.c's too) */
unsigned long long SDL_GetTicksNS(void);
void platform_log(char const *format, ...);
/* cinematics.c's */
extern struct cinematic_global_data *cinematic_globals;
/* menu_tags.c's */
boolean pc_menu_map_frame_at(unsigned long address, long width, long height, long *tag_index, long *frame);

/* ---------- globals */

static struct
{
	enum render_state state;
	long pose;
	unsigned long long since;
	/* (when a cinematic was last seen running) */
	unsigned long long cinematic_seen;
	long frames;
	long step;
	long step_count;
	struct
	{
		enum render_step_kind kind;
		long amount;
		char line[MAXIMUM_RENDER_LINE];
	} steps[MAXIMUM_RENDER_STEPS];
} menu_picture_render;

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

/* ---------- the copy that renders a picture */

static unsigned long long render_milliseconds(
	void)
{
	return SDL_GetTicksNS() / 1000000ULL;
}

static void render_step_add(
	enum render_step_kind kind,
	long amount,
	char const *line)
{
	if (menu_picture_render.step_count >= MAXIMUM_RENDER_STEPS)
		return;
	menu_picture_render.steps[menu_picture_render.step_count].kind = kind;
	menu_picture_render.steps[menu_picture_render.step_count].amount = amount;
	snprintf(menu_picture_render.steps[menu_picture_render.step_count].line, MAXIMUM_RENDER_LINE, "%s",
		line ? line : "");
	menu_picture_render.step_count++;
}

/* the scene's steps, from the console as the pose was found */
static void render_steps_make(
	struct menu_picture_pose const *picture)
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
	char line[MAXIMUM_RENDER_LINE];
	char const *commands = picture->commands;
	short index;

	menu_picture_render.step_count = 0;
	for (index = 0; index < NUMBEROF(scene); index++)
		render_step_add(_step_command, 0, scene[index]);
	render_step_add(_step_milliseconds, 1500, NULL);
	render_step_add(_step_command, 0, "game_speed 0");
	render_step_add(_step_quiet, 0, NULL);
	/* (the first camera set takes the script's camera: a frame later) */
	render_step_add(_step_command, 0, "debug_camera_set \"0 0 0 0 0 0 50\"");
	render_step_add(_step_frames, 4, NULL);
	snprintf(line, sizeof(line), "switch_bsp %ld", picture->bsp);
	render_step_add(_step_command, 0, line);
	render_step_add(_step_frames, 30, NULL);
	render_step_add(_step_command, 0, "debug_camera_set \"0 0 0 0 0 0 50\"");
	render_step_add(_step_frames, 4, NULL);
	while (commands && *commands)
	{
		char const *end = strchr(commands, '\n');
		long length = end ? (long)(end - commands) : (long)strlen(commands);

		if (length >= MAXIMUM_RENDER_LINE)
			length = MAXIMUM_RENDER_LINE - 1;
		memcpy(line, commands, length);
		line[length] = 0;
		render_step_add(_step_command, 0, line);
		render_step_add(_step_frames, 3, NULL);
		commands = end ? end + 1 : NULL;
	}
	/* (a vehicle comes to rest on the ground, a rider takes its seat) */
	if (picture->objects)
	{
		render_step_add(_step_command, 0, "game_speed 1");
		render_step_add(_step_milliseconds, 3000, NULL);
		render_step_add(_step_command, 0, "game_speed 0");
		render_step_add(_step_frames, 3, NULL);
	}
	snprintf(line, sizeof(line), "debug_camera_set \"%s\"", picture->camera);
	render_step_add(_step_command, 0, line);
	render_step_add(_step_frames, RENDER_POSE_FRAMES, NULL);
	render_step_add(_step_quiet, 0, NULL);
	render_step_add(_step_milliseconds, RENDER_POSE_MILLISECONDS, NULL);
	render_step_add(_step_capture, 0, NULL);
}

/* the world stopped, whatever a cinematic its scripts started meanwhile
left on the screen gone: its letterbox (which goes as the world's time
passes) and its titles */
static void render_quiet(
	void)
{
	short index;

	if (!cinematic_globals)
		return;
	cinematic_globals->show_letterbox = FALSE;
	cinematic_globals->letterbox_amount = 0.0f;
	for (index = 0; index < NUMBEROF(cinematic_globals->queued_titles); index++)
		cinematic_globals->queued_titles[index].title_index = NONE;
}

static void render_failed(
	char const *why)
{
	platform_log("menu pictures: %s", why);
	menu_pictures_job_end(_menu_picture_failed, 0);
}

/* the original frame's texels, loaded now (ui.map's, in the main menu) */
static void render_keep_original(
	struct menu_picture_pose const *picture)
{
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
		menu_pictures_keep_original(texture);
}

static void render_update(
	int main_menu_loaded)
{
	struct menu_picture_pose const *picture;
	unsigned long long now = render_milliseconds();
	char line[MAXIMUM_RENDER_LINE];

	if (menu_picture_render.state == _render_start)
	{
		if (!main_menu_loaded)
			return;
		menu_picture_render.pose = menu_pictures_job_pose();
		if (menu_picture_render.pose < 0)
			render_failed("no such pose");
		picture = &menu_picture_poses[menu_picture_render.pose];
		if (picture->colour_curve)
			render_keep_original(picture);
		render_steps_make(picture);
		snprintf(line, sizeof(line), "map_name %s", picture->scenario);
		platform_log("menu pictures: rendering %s: %s", picture->name, line);
		hs_compile_and_evaluate(line);
		menu_picture_render.state = _render_loading;
		menu_picture_render.since = now;
		return;
	}
	picture = &menu_picture_poses[menu_picture_render.pose];
	switch (menu_picture_render.state)
	{
	case _render_loading:
		if (!main_menu_loaded && game_in_progress() && !strcasecmp(cache_file_loaded_map_name(), picture->map))
		{
			unsigned long checksum = cache_files_get_checksum();
			long index;
			boolean made_for = FALSE;

			for (index = 0; index < picture->checksum_count; index++)
				made_for |= picture->checksums[index] == checksum;
			if (!made_for)
			{
				platform_log("menu pictures: %s's checksum %08lx is not the pose's", picture->map, checksum);
				menu_pictures_job_end(_menu_picture_modded, checksum);
			}
			platform_log("menu pictures: %s loaded in %.1f s", picture->map,
				(double)(now - menu_picture_render.since) / 1000.0);
			menu_pictures_set_map_checksum(checksum);
			menu_picture_render.state = _render_settling;
			menu_picture_render.since = now;
			menu_picture_render.cinematic_seen = now;
		}
		else if (now - menu_picture_render.since > RENDER_LOAD_LONGEST)
		{
			render_failed("the map did not load");
		}
		break;
	case _render_settling:
		/* (its opening cinematics skipped, debug.skip_cinematics, or run
		fast: none for RENDER_SETTLE, and the scripts' fades after them done) */
		if (cinematic_in_progress())
			menu_picture_render.cinematic_seen = now;
		if (now - menu_picture_render.since < RENDER_CINEMATIC_LONGEST &&
			(now - menu_picture_render.since < RENDER_SETTLE || now - menu_picture_render.cinematic_seen < RENDER_SETTLE))
		{
			break;
		}
		platform_log("menu pictures: the scene set after %.1f s%s", (double)(now - menu_picture_render.since) / 1000.0,
			cinematic_in_progress() ? " (a cinematic still running)" : "");
		menu_picture_render.state = _render_steps;
		menu_picture_render.step = 0;
		menu_picture_render.frames = 0;
		menu_picture_render.since = now;
		break;
	case _render_steps:
		while (menu_picture_render.step < menu_picture_render.step_count)
		{
			long step = menu_picture_render.step;

			switch (menu_picture_render.steps[step].kind)
			{
			case _step_command:
				hs_compile_and_evaluate(menu_picture_render.steps[step].line);
				menu_picture_render.step++;
				menu_picture_render.frames = 0;
				menu_picture_render.since = now;
				/* (a line a frame) */
				return;
			case _step_frames:
				if (++menu_picture_render.frames < menu_picture_render.steps[step].amount)
					return;
				break;
			case _step_milliseconds:
				if (now - menu_picture_render.since < (unsigned long long)menu_picture_render.steps[step].amount)
					return;
				break;
			case _step_quiet:
				render_quiet();
				break;
			case _step_capture:
				menu_pictures_capture_request();
				menu_picture_render.state = _render_capturing;
				menu_picture_render.since = now;
				return;
			}
			menu_picture_render.step++;
			menu_picture_render.frames = 0;
			menu_picture_render.since = now;
		}
		break;
	case _render_capturing:
		if (now - menu_picture_render.since > RENDER_CAPTURE_LONGEST)
			render_failed("no frame was kept");
		break;
	default:
		break;
	}
}

void menu_pictures_frame(
	int main_menu_loaded)
{
	if (menu_pictures_rendering())
		render_update(main_menu_loaded);
	else
		menu_pictures_update(main_menu_loaded);
}
