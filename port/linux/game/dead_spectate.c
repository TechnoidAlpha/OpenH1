/*
DEAD_SPECTATE.C

A dead player watching a living teammate until they respawn
(game.spectate_on_death). With "teammates" (off by default) a dead player in
co-op (network or split screen) or in a team game sees through a living
teammate's eyes, full screen: the teammate's eye, their aim, their zoom,
and their first-person weapon and arms, their body hidden from that view,
as their own player sees them (not their HUD: no crosshair, meters or
scope's mask; the dead player's own HUD says whom they watch). It is drawn from this machine's copy of the
world, as every other view is: nothing is sent, and nothing the machines of
a network game agree on changes. A free-for-all game has no teammates and
shows nobody's view (the game's own death camera, which orbits the body);
nor does a game with no living teammate.

The view is a camera of the director's (director.c), so the observer, the
frames' interpolation between ticks, the sound's listener and the HUD's
marks follow it as they follow a player's own first-person camera. The
host sends a far player's aim less often than a near one's (the
distributed netcode's priorities), so the aim is eased toward the unit's
over a few frames rather than stepping with each update. A teammate in a
seat whose camera follows the vehicle (a Warthog's driver, a Ghost) is
watched from behind instead, as network co-op's spectating did before
(coop_spectate.c): their own player sees no eyes' view there either.

A press of A (or the keyboard's jump key, which a dead player's input
does nothing else with) goes to the next living teammate; when the one
watched dies the view goes to the next, or with none, to the death camera.
Each local player of split screen has its own choice and its own window.
Respawning, a cutscene (the scripted camera) and the game's end hand the
view back (director.c).

With "off", the game's death camera, and network co-op's view of a
teammate from behind (coop_spectate.c), as before.
*/

#include "cseries.h"
#include "camera/director.h"
#include "camera/observer.h"
#include "camera/first_person_camera.h"
#include "camera/static_camera.h"
#include "cutscene/cinematics.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "input/input.h"
#include "interface/hud_messaging.h"
#include "items/weapons.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"
#include "units/units.h"
#include "units/unit_definitions.h"

#include "dead_spectate.h"
#include "network_coop.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- constants */

/* how fast the view's aim eases toward the teammate's unit's: the time for
it to close all but a third of the way (seconds). Short of a near
teammate's updates (each tick) lagging, long enough to glide over a far
one's (a few ticks apart) */
#define DEAD_SPECTATE_AIM_TIME 0.06f

/* ---------- structures */

/* director.c's camera data (its camera_data, 0x40 bytes) */
struct dead_spectate_camera
{
	long unit_index;
	short local_player_index;
	boolean aim_valid;
	byte pad;
	real_vector3d forward;
	real_vector3d up;
	real field_of_view;
};

typedef char dead_spectate_camera_size_assert[sizeof(struct dead_spectate_camera) <= 0x40 ? 1 : -1];

/* the director's controls (director.c's struct camera_control): their start */
struct dead_spectate_controls
{
	short local_player_index;
	boolean active;
	byte pad3;
	real seconds_elapsed;
};

/* the director's result for a first-person camera (first_person_camera.c's
struct first_person_camera_result): a camera command, and the field of
view's transition */
struct dead_spectate_result
{
	struct camera_command command;
	byte reserved4C[3];
	boolean field_of_view_timed;
	byte reserved50[0x10];
	real field_of_view_time;
};

/* ---------- prototypes */

/* port/linux/src/port_config.c */
char const *config_string(char const *name);
int config_boolean(char const *name);
void platform_log(char const *format, ...);

/* port/linux/src/d3d8_device.c: the next frame saved as
debug.screenshot_directory's <name>.bmp */
void halo_screenshot_named(const char *name);

/* director.c: a local player's camera data if its camera is camera_proc,
else NULL */
void *director_port_camera_data(short local_player_index, void *camera_proc);

/* port/linux/game/hud_key_prompts.c, port/linux/src/xinput_sdl.c */
boolean hud_key_prompt_text(short local_player_index, short game_control, short gamepad_button, wchar_t *text,
	long length);
int halo_gamepad_last_used(void);

/* input_abstraction.c: how many ticks the keyboard's jump key has been held */
byte input_abstraction_port_accept(short controller_index);

/* ---------- globals */

/* the player each local player is watching, or NONE */
static long dead_spectate_watched[MAXIMUM_LOCAL_PLAYERS] = { NONE, NONE, NONE, NONE };

/* ---------- private code */

/* game.spectate_on_death is "teammates" */
static boolean spectate_teammates(
	void)
{
	return strcmp(config_string("game.spectate_on_death"), "off") != 0;
}

/* whether this game has teammates to watch: co-op (a campaign map, no game
engine) or a team game; network co-op's always does */
static boolean spectate_game(
	void)
{
	if (network_coop_active())
		return TRUE;
	if (!spectate_teammates() || game_connection() == _game_connection_film_playback)
		return FALSE;
	return game_engine_running() ? game_engine_has_teams() && game_engine_in_play() : TRUE;
}

/* whether other may be watched by self: a living player of self's team
(co-op: anyone) */
static boolean spectate_teammate(
	long self,
	long other)
{
	struct player_datum *player = player_try_and_get(other);

	if (!player || other == self || player->unit_index == NONE || !object_try_and_get(player->unit_index))
		return FALSE;
	return !game_engine_running() || player->team_index == player_get(self)->team_index;
}

/* The next living teammate after `after` (or the first, if `after` is
NONE), skipping `self` and wrapping around; NONE if none is alive */
static long next_teammate(
	long self,
	long after)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long first = NONE;
	boolean passed = after == NONE;

	data_iterator_new(&iterator, player_data);
	while ((player = data_iterator_next(&iterator)) != NULL)
	{
		if (iterator.datum_index == after)
		{
			passed = TRUE;
			continue;
		}
		if (!spectate_teammate(self, iterator.datum_index))
			continue;
		if (passed)
			return iterator.datum_index;
		if (first == NONE)
			first = iterator.datum_index;
	}

	return first;
}

/* Returns TRUE once per press of A (or jump on the keyboard), however long
it is held. The buttons count ticks, and one tick can span several frames,
so testing "held" alone would switch more than once. */
static boolean next_pressed(
	short controller_index)
{
	static boolean held[MAXIMUM_GAMEPADS];
	struct gamepad_state const *gamepad;
	boolean down;
	boolean pressed;

	if (controller_index < 0 || controller_index >= MAXIMUM_GAMEPADS)
		return FALSE;
	gamepad = input_get_gamepad_state(controller_index);
	down = (gamepad && gamepad->buttons[FIRST_GAMEPAD_ANALOG_BUTTON + _gamepad_analog_button_a] > 0) ||
		input_abstraction_port_accept(controller_index) > 0;
	pressed = down && !held[controller_index];
	held[controller_index] = down;
	return pressed;
}

/* the view's field of view: the teammate's weapon's at its zoom, as
player_control_get_field_of_view gives a player's own (the port's
display.field_of_view widens it as it draws, as it does every view's) */
static real spectate_field_of_view(
	long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct unit_definition *definition = unit_definition_get(unit->definition_index);
	long weapon_index = unit_inventory_get_weapon(unit_index, unit->unit.current_weapon_index);
	real field_of_view = definition->unit.camera_field_of_view;

	if (weapon_index != NONE)
		field_of_view = weapon_get_field_of_view(weapon_index, field_of_view, (short)unit->unit.current_zoom_level);
	if (!(field_of_view >= 0.001f && field_of_view <= _pi / 2.f))
		field_of_view = DEGREES_TO_RADIANS(70.f);
	return field_of_view;
}

/* the vector from `from` a fraction of the way to `to`, made a unit
vector */
static void ease_vector(
	real_vector3d *from,
	real_vector3d const *to,
	real fraction)
{
	from->i += (to->i - from->i) * fraction;
	from->j += (to->j - from->j) * fraction;
	from->k += (to->k - from->k) * fraction;
	if (normalize3d(from) <= 0.0001f)
		*from = *to;
}

/* ---------- the tests' log and pictures */

/* debug.spectate_log: each tick, each local player's camera as its
observer has it, to the log (where it looks from, and along), whether it is dead and
whom it watches; debug.spectate_shots ("<tick>,<tick>..."): the frame at
the start of each of those ticks saved as debug.screenshot_directory's
tick<tick>.bmp. Two machines' logs at a tick compare the view of a
teammate with the teammate's own (tools that read it: the PR's proof) */
static struct
{
	boolean read;
	boolean log;
	long logged_tick[MAXIMUM_LOCAL_PLAYERS];
	long shots[64];
	short shot_count;
	short next_shot;
} dead_spectate_debug;

static void debug_frame(
	short local_player_index,
	long watched)
{
	long tick = game_time_get();

	if (!dead_spectate_debug.read)
	{
		char const *list;
		short index;

		dead_spectate_debug.read = TRUE;
		dead_spectate_debug.log = config_boolean("debug.spectate_log");
		for (index = 0; index < MAXIMUM_LOCAL_PLAYERS; index++)
			dead_spectate_debug.logged_tick[index] = NONE;
		list = config_string("debug.spectate_shots");
		while (list && *list && dead_spectate_debug.shot_count < (short)NUMBEROF(dead_spectate_debug.shots))
		{
			char *end;
			long shot = strtol(list, &end, 10);

			if (end == list)
				break;
			dead_spectate_debug.shots[dead_spectate_debug.shot_count++] = shot;
			list = *end == ',' ? end + 1 : end;
		}
	}
	if (!game_in_progress())
		return;
	if (dead_spectate_debug.log && dead_spectate_debug.logged_tick[local_player_index] != tick)
	{
		struct observer_result const *camera = observer_get_camera(local_player_index);
		long self = local_player_get_player_index(local_player_index);

		dead_spectate_debug.logged_tick[local_player_index] = tick;
		platform_log("spectate: %ld %d %ld %d %ld %d %.4f %.4f %.4f %.5f %.5f %.5f %.4f", tick,
			(int)local_player_index, self == NONE ? -1L : (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(self),
			self != NONE && player_get(self)->unit_index == NONE,
			watched == NONE ? -1L : (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(watched),
			dead_spectate_eyes_unit(local_player_index) != NONE,
			camera->position.x, camera->position.y, camera->position.z,
			camera->forward.i, camera->forward.j, camera->forward.k, camera->field_of_view);
	}
	if (local_player_index == 0 && dead_spectate_debug.next_shot < dead_spectate_debug.shot_count &&
		tick >= dead_spectate_debug.shots[dead_spectate_debug.next_shot])
	{
		char name[32];

		snprintf(name, sizeof(name), "tick%06ld", dead_spectate_debug.shots[dead_spectate_debug.next_shot]);
		halo_screenshot_named(name);
		dead_spectate_debug.next_shot++;
	}
}

/* ---------- public code */

long dead_spectate_unit(
	short local_player_index)
{
	long self;
	long *watched;
	struct player_datum *player;
	boolean next;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
		return NONE;
	self = local_player_get_player_index(local_player_index);
	watched = &dead_spectate_watched[local_player_index];
	if (self == NONE)
		return NONE;
	player = player_get(self);
	/* checked every frame so a press is never missed or counted twice */
	next = next_pressed(player->network_player_data.controller_index);
	debug_frame(local_player_index, player->unit_index == NONE ? *watched : NONE);
	if (next && dead_spectate_debug.log)
		platform_log("spectate: local player %d pressed next", (int)local_player_index);
	/* alive: keep `watched`, so the next death starts on the same teammate */
	if (player->unit_index != NONE || !spectate_game())
		return NONE;
	if (!spectate_teammate(self, *watched))
		*watched = next_teammate(self, NONE);
	else if (next)
	{
		/* (the one watched, if they are the only one) */
		long after = next_teammate(self, *watched);

		if (after != NONE)
			*watched = after;
	}

	return *watched != NONE ? player_get(*watched)->unit_index : NONE;
}

long dead_spectate_watched_player(
	short local_player_index)
{
	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
		return NONE;
	return dead_spectate_watched[local_player_index];
}

boolean dead_spectate_through_eyes(
	long unit_index)
{
	director_perspective perspective;

	if (unit_index == NONE || !spectate_teammates() || !object_try_and_get(unit_index))
		return FALSE;
	return !director_desired_perspective(unit_index, &perspective);
}

void dead_spectate_camera_new(
	void *data,
	short local_player_index,
	long unit_index)
{
	struct dead_spectate_camera *camera = (struct dead_spectate_camera *)data;

	csmemset(camera, 0, sizeof(*camera));
	camera->unit_index = unit_index;
	camera->local_player_index = local_player_index;
	camera->aim_valid = FALSE;
}

long dead_spectate_camera_unit(
	void const *data)
{
	return ((struct dead_spectate_camera const *)data)->unit_index;
}

void dead_spectate_camera_update(
	void *data,
	void *control_data,
	void *result_data)
{
	struct dead_spectate_camera *camera = (struct dead_spectate_camera *)data;
	struct dead_spectate_controls const *controls = (struct dead_spectate_controls const *)control_data;
	struct dead_spectate_result *result = (struct dead_spectate_result *)result_data;
	real field_of_view;

	if (camera->unit_index == NONE || !object_try_and_get_and_verify_type(camera->unit_index, _object_mask_unit))
		return;
	/* the teammate's first-person camera, as theirs is made from their unit */
	first_person_camera_fake(camera->unit_index, &result->command);
	if (!camera->aim_valid)
	{
		camera->forward = result->command.forward;
		camera->up = result->command.up;
		camera->aim_valid = TRUE;
	}
	else
	{
		real seconds = controls ? controls->seconds_elapsed : 0.f;
		real fraction = seconds > 0.f ? 1.f - (real)exp(-seconds / DEAD_SPECTATE_AIM_TIME) : 0.f;
		real along;

		ease_vector(&camera->forward, &result->command.forward, fraction);
		ease_vector(&camera->up, &result->command.up, fraction);
		/* (up square to forward again) */
		along = dot_product3d(&camera->up, &camera->forward);
		camera->up.i -= camera->forward.i * along;
		camera->up.j -= camera->forward.j * along;
		camera->up.k -= camera->forward.k * along;
		if (normalize3d(&camera->up) <= 0.0001f)
			observer_up_from_forward(&camera->forward, &camera->up);
	}
	result->command.forward = camera->forward;
	result->command.up = camera->up;
	field_of_view = spectate_field_of_view(camera->unit_index);
	result->command.field_of_view = field_of_view;
	/* (a zoom eases, as first_person_camera_update eases a player's own) */
	if (camera->field_of_view != 0.f && camera->field_of_view != field_of_view)
	{
		result->field_of_view_time = 0.18f;
		result->field_of_view_timed = TRUE;
	}
	camera->field_of_view = field_of_view;
}

long dead_spectate_eyes_unit(
	short local_player_index)
{
	struct dead_spectate_camera const *camera;
	long player_index;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS)
		return NONE;
	player_index = local_player_get_player_index(local_player_index);
	if (player_index == NONE || player_get(player_index)->unit_index != NONE)
		return NONE;
	camera = (struct dead_spectate_camera const *)director_port_camera_data(
		local_player_index, (void *)dead_spectate_camera_update);
	if (!camera || camera->unit_index == NONE || !object_try_and_get(camera->unit_index))
		return NONE;
	return camera->unit_index;
}

long dead_spectate_first_person_weapon_unit(
	short local_player_index,
	long unit_index)
{
	return unit_index != NONE ? unit_index : dead_spectate_eyes_unit(local_player_index);
}

/* centred text near the bottom of the screen, in the HUD's font */
static void draw_bottom_text(
	wchar_t const *text)
{
	long font_index = hud_get_font_index();
	struct font_header *font;
	real_argb_color color = *global_real_argb_white;
	rectangle2d bounds;
	short height;

	if (font_index == NONE)
		return;
	font = font_definition_get(font_index);
	height = (short)(font->ascending_height + font->descending_height);
	/* (the HUD's coordinates are the view's own: a split screen's lower
	window's too, as hud_draw_match_timer's) */
	bounds = render.camera.window_bounds;
	offset_rectangle2d(&bounds, (short)-render.camera.viewport_bounds.x0, (short)-render.camera.viewport_bounds.y0);
	bounds.y0 = (short)(bounds.y1 - 3 * height - 24);
	bounds.y1 = (short)(bounds.y1 - 24);
	/* draw mode 2: centered */
	draw_string_set_draw_mode(font_index, NONE, 2, 0, &color);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, text);
}

void dead_spectate_draw(
	short local_player_index)
{
	long watched = dead_spectate_watched_player(local_player_index);
	struct player_datum *player;
	wchar_t text[128];
	wchar_t button[32];
	wchar_t const *hint;

	if (dead_spectate_eyes_unit(local_player_index) == NONE || watched == NONE)
		return;
	player = player_try_and_get(watched);
	if (!player)
		return;
	/* the button that goes to the next: A, or for the first player while
	they play with the keyboard, the key A's control (jump) is bound to, as
	the HUD's prompts name it (hud_key_prompts.c) */
	csmemcpy(button, L"A", 2 * sizeof(wchar_t));
	if (local_player_index == local_player_get_next(NONE) && !halo_gamepad_last_used() &&
		hud_key_prompt_text(local_player_index, NONE, 0 /* A */, button, NUMBEROF(button)))
	{
		short index;

		for (index = 0; index < (short)NUMBEROF(button) && button[index]; index++)
		{
			if (button[index] >= L'a' && button[index] <= L'z')
				button[index] = (wchar_t)(button[index] - L'a' + L'A');
		}
	}
	/* (network co-op: when the dead come back, as coop_spectate_draw says) */
	if (!network_coop_active())
		hint = L"";
	else if (!players_coop_waiting_to_start(local_player_get_player_index(local_player_index)))
		hint = L"\r\nYou come back beside them once it is safe";
	else
		hint = L"\r\nYou join them when they're on foot";
	usnprintf(text, NUMBEROF(text) - 1, L"WATCHING %.12s   %s: NEXT TEAMMATE%s", player->name, button, hint);
	text[NUMBEROF(text) - 1] = 0;
	draw_bottom_text(text);
}
