/*
SPARTAN_LINK.C

The HUD's Spartan link (display.spartan_link): live feeds of the living
teammates' views in the HUD's top line, between the ammunition's panel at
the top left and the shield's at the top right. A key shows and hides it
(controls.spartan_link, B; on a controller in the stock layouts, D-pad
Down), off at the start of every game. Co-op (network and split screen)
and team games only, teammates only; a free-for-all game, and a Pro 2V2
game, have none, and the key says so on the HUD.

Each feed is the teammate's first-person view as this machine has the
world: their eye (where their body is drawn, render_interpolation.c), their
aim, their weapon's zoom, their weapon and arms, their body hidden, and
none of their HUD. The weapon and arms move as on the teammate's own
screen: first_person_weapons.c animates each linked teammate's in a slot of
its own beside the local players' (its feeds' slots), from the same
messages of their weapon and unit a local player's slot takes (every
machine plays its teammates from their inputs, so each shot's kick and
firing push back, a reload, empty or full, a melee, a grenade's throw, a
weapon put away and readied, overheating and its vent, a charge, come as on
their own machine) and from their unit as this machine has it each tick
(the weapon in their hand, their throttle's moving overlay, their aim's
turning sway). It is hidden while they zoom, as on their screen. Drawn at
the frame's time, so it moves every frame though the feed is drawn in turn. A teammate in a seat whose camera follows the vehicle
is seen from behind. Under each: their name, their shield, their health
under it (the HUD's health colours, flashing as the HUD's does while their
shield is down) and their distance.

On each feed, the teammate's reticle: a dot in a thin ring where they aim,
in the HUD's blue, red with an enemy under it as Halo's turns red (a living
enemy biped, within their weapon's autoaim reach, that a line along their
aim meets first: a collision test of this machine's world, which takes no
seed and makes nothing). Through their eyes it is the feed's middle, the
feed looking along their aim; from behind them in a gunner's seat, the
point their aim meets, seen through the feed's camera. None in a seat
without a weapon (a driver's or a passenger's), nor while they are down.

A teammate who dies is shown down for a moment, then dropped;
with none left alive the link closes. While this player is dead the link
is hidden (spectating fills the screen: dead_spectate.c) and comes back
with the respawn. Split screen: each window has its own link, of its own
player's teammates.

The cost: one feed is drawn a frame (render.c's render_frame, before the
window's view), each kept in its own images between (d3d8_device.c's extra
views, halo_view_begin) at the size it is drawn on the screen, without the
anti-aliasing pass, multisampling, the world's ambient occlusion, the
objects' shadows, lens flares or the weather, and its models' levels of
detail chosen for that size. So the frame pays for about one small view,
however many feeds there are; with the link hidden, nothing.

Only what this machine draws: nothing is sent, the game state is not
touched (no seed is taken, no effect or sound made: the feeds' slots are
first_person_weapons.c's own, beside the game state, with their own idle
poses' generator), and the network version is unchanged. The feeds' slots'
animations step once a tick, a slot's few nodes, for each linked teammate.
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
#include "interface/hud.h"
#include "interface/hud_messaging.h"
#include "interface/ui_widget.h"
#include "game/game_allegiance.h"
#include "interface/hud_definitions.h"
#include "interface/unit_hud_interface_definition.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "physics/collisions.h"
#include "physics/collision_usage.h"
#include "main/console.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "render/render_cameras.h"
#include "scenario/scenario.h"
#include "game/game_globals.h"
#include "bitmaps/bitmaps.h"
#include "bitmaps/bitmap_group.h"
#include "bitmaps/bitmap_color_conversion.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"
#include "units/units.h"
#include "units/unit_definitions.h"
#include "units/biped_definitions.h"

#include "halo_keyboard.h"
#include "controller_remap.h"
#include "far_clip.h"
#include "network_coop.h"
#include "network_standard.h"
#include "spartan_link.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---------- prototypes */

/* port/linux/src/port_config.c */
char const *config_string(char const *name);
int config_boolean(char const *name);
unsigned long config_changes(void);
void platform_log(char const *format, ...);
unsigned long system_milliseconds(void);

/* port/linux/src/d3d8_device.c: the extra views */
void halo_view_begin(int view, unsigned long width, unsigned long height);
void halo_view_end(void);
int halo_view_drawn(int view);
void halo_view_sample(int view);
void halo_view_release(int view);
float halo_screen_pixel_scale(void);
long halo_screen_width(void);

/* port/linux/src/d3d8_device.c: the next frame saved as
debug.screenshot_directory's <name>.bmp */
void halo_screenshot_named(const char *name);

/* source/objects/object_lights.c */
void lights_port_keep(boolean keep);

/* port/linux/game/render_interpolation.c */
void render_interpolation_unit_camera_position(long unit_index, real_point3d *position);
real render_interpolation_fraction(void);
real render_interpolation_first_person_feed_phase(void);

/* port/linux/src/xinput_sdl.c */
int halo_spartan_link_pad_pressed(short port);

/* port/linux/game/network_chat.c */
boolean network_chat_typing(void);

/* ---------- constants */

#define SPARTAN_LINK_FEEDS 4
/* (the extra views' numbers: 1 + a local player's 4) */
#define SPARTAN_LINK_VIEW(local_player_index, feed) (1 + (local_player_index) * SPARTAN_LINK_FEEDS + (feed))

/* how long a teammate who died is shown down before their feed drops out,
and a refusal's line shown (milliseconds) */
#define SPARTAN_LINK_DOWN_MILLISECONDS 1500
#define SPARTAN_LINK_REFUSAL_MILLISECONDS 2000

/* the layout, in the screen's units of a window 480 tall (the HUD's): each
feed's width for one to four feeds, the gap between them, the top (from the
view's top edge, not the title-safe frame's), and the row under each (name,
shield, distance). At 1600x900 these are the design's 300, 260, 228 and 210
pixels */
static real const spartan_link_widths[SPARTAN_LINK_FEEDS] = { 160.0f, 138.7f, 121.6f, 112.0f };
#define SPARTAN_LINK_GAP 6.4f
#define SPARTAN_LINK_TOP 11.0f
#define SPARTAN_LINK_ROW 11.0f
/* what the HUD's panels take of each side of the top line, which the feeds
stay clear of (the stock HUD's and the high-res HUD's, at the HUD's scale) */
#define SPARTAN_LINK_PANEL_LEFT 180.0f
#define SPARTAN_LINK_PANEL_RIGHT 198.0f
/* the frame's angled ends: how far in its top corners are, of its height */
#define SPARTAN_LINK_SLANT 0.12f
/* the teammate's reticle on their feed: its ring's radius, of the feed's
height, and its curve's steps; and its red, on an enemy (Halo's reticle's) */
#define SPARTAN_LINK_RETICLE_SIZE 0.05f
#define SPARTAN_LINK_RETICLE_SEGMENTS 16
#define SPARTAN_LINK_RETICLE_RED 0xF2FF3424UL

/* ---------- structures */

struct spartan_link_feed
{
	long player_index;
	/* the unit they were last seen alive in */
	long unit_index;
	/* when they were seen dead (0: alive) */
	unsigned long down_at;
	/* the view's size in the screen's units, as last drawn */
	unsigned long width, height;
	/* since when their health has flashed, their shield down (0: not) */
	unsigned long health_flash_at;
	/* (debug.spartan_link_log's: the reticle (0 none, 1 blue, 2 red) and
	the health last logged) */
	short logged_state;
	real logged_health;
	/* their aim as the game has it, this frame (spartan_link_feed_aim_update) */
	boolean aim_shown, aim_enemy;
	real_point3d aim_target;
};

struct spartan_link_window
{
	boolean open;
	short count;
	struct spartan_link_feed feeds[SPARTAN_LINK_FEEDS];
	/* a refusal's line (free for all, Pro 2V2), and since when */
	wchar_t const *refusal;
	unsigned long refusal_at;
};

static struct
{
	boolean in_game;
	long last_game_time;
	unsigned long read_at;
	boolean enabled;
	real size;
	boolean log;
	boolean debug_open;
	long logged_tick;
	struct spartan_link_window windows[MAXIMUM_LOCAL_PLAYERS];
	boolean key_held;
	/* the feed drawn this frame (local player, feed; NONE none), and the
	one drawn last, which the next follows round */
	short due_local_player, due_feed;
	short last_local_player, last_feed;
	/* the feed being drawn: whose eyes hide whose body, and whose weapon is
	posed (render_objects.c, first_person_weapons.c) */
	boolean drawing;
	long hidden_unit;
	long posed_unit;
	/* the views drawn in this game (to free them as it ends) */
	unsigned long views_used;
	/* (debug.spartan_link_log: a red reticle's frame saved, once a run) */
	boolean red_saved;
} spartan_link = { FALSE, 0, (unsigned long)-1, TRUE, 1.0f, FALSE };

/* ---------- private code */

/* a game in progress (and the game's time made: not before the first map) */
static boolean spartan_link_playing(
	void)
{
	return game_time_initialized() && game_in_progress();
}

static void spartan_link_read(
	void)
{
	char const *size;

	if (spartan_link.read_at == config_changes())
		return;
	spartan_link.read_at = config_changes();
	size = config_string("display.spartan_link");
	spartan_link.enabled = strcmp(size, "off") != 0;
	spartan_link.size = !strcmp(size, "small") ? 0.8f : !strcmp(size, "large") ? 1.2f : 1.0f;
	spartan_link.log = config_boolean("debug.spartan_link_log");
}

/* a game whose host allows no teammates' feeds: Pro 2V2's, Classic and
Crispy (network_standard_teammate_feeds_allowed) */
static boolean spartan_link_pro_doubles(
	void)
{
	return !network_standard_teammate_feeds_allowed();
}

/* whether this game has a link: co-op (a campaign, network or split
screen) or a team game, in play, and not Pro 2V2 */
static boolean spartan_link_game(
	void)
{
	if (!spartan_link.enabled || !spartan_link_playing() || cinematic_in_progress() ||
		game_connection() == _game_connection_film_playback)
	{
		return FALSE;
	}
	if (network_coop_active())
		return TRUE;
	return game_engine_running() ? game_engine_has_teams() && game_engine_in_play() &&
		!spartan_link_pro_doubles() : TRUE;
}

/* other is self's living teammate (co-op: anyone) */
static boolean spartan_link_teammate(
	long self,
	long other)
{
	struct player_datum *player = player_try_and_get(other);

	if (!player || other == self || player->unit_index == NONE ||
		!object_try_and_get_and_verify_type(player->unit_index, _object_mask_unit))
	{
		return FALSE;
	}
	if (TEST_FLAG(object_get(player->unit_index)->object.damage_flags, _object_dead_bit))
		return FALSE;
	return !game_engine_running() || player->team_index == player_get(self)->team_index;
}

/* the window's feeds brought up to date: the downed kept for a moment,
then dropped; the living added in the players' order, up to four */
static void spartan_link_window_update(
	short local_player_index,
	unsigned long now)
{
	struct spartan_link_window *window = &spartan_link.windows[local_player_index];
	long self = local_player_get_player_index(local_player_index);
	struct data_iterator iterator;
	struct player_datum *player;
	short index;

	if (self == NONE)
	{
		window->count = 0;
		window->open = FALSE;
		return;
	}
	for (index = 0; index < window->count; )
	{
		struct spartan_link_feed *feed = &window->feeds[index];
		struct player_datum *teammate = player_try_and_get(feed->player_index);

		if (spartan_link_teammate(self, feed->player_index) && teammate->unit_index == feed->unit_index)
		{
			feed->down_at = 0;
		}
		else if (!feed->down_at && teammate)
		{
			feed->down_at = now ? now : 1;
		}
		if (!teammate || (feed->down_at && now - feed->down_at >= SPARTAN_LINK_DOWN_MILLISECONDS))
		{
			short later;

			for (later = index; later + 1 < window->count; later++)
				window->feeds[later] = window->feeds[later + 1];
			window->count--;
			continue;
		}
		index++;
	}
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL &&
		window->count < SPARTAN_LINK_FEEDS)
	{
		boolean listed = FALSE;

		if (!spartan_link_teammate(self, iterator.datum_index))
			continue;
		for (index = 0; index < window->count; index++)
		{
			if (window->feeds[index].player_index == iterator.datum_index)
				listed = TRUE;
		}
		if (listed)
			continue;
		csmemset(&window->feeds[window->count], 0, sizeof(window->feeds[window->count]));
		window->feeds[window->count].player_index = iterator.datum_index;
		window->feeds[window->count].unit_index = player->unit_index;
		window->count++;
	}
	/* (nobody left alive: the link closes; debug.spartan_link_open's opens
	again as they come) */
	if (window->open && window->count == 0 && !spartan_link.debug_open)
		window->open = FALSE;
	if (spartan_link.debug_open && window->count > 0)
		window->open = TRUE;
}

/* the window's player is alive, and sees the link */
static boolean spartan_link_shown(
	short local_player_index)
{
	long self;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS ||
		!spartan_link.windows[local_player_index].open || !spartan_link_game())
	{
		return FALSE;
	}
	self = local_player_get_player_index(local_player_index);
	return self != NONE && player_get(self)->unit_index != NONE;
}

/* a press of the key for a local player: the link shown or hidden, or why
there is none */
static void spartan_link_toggle(
	short local_player_index,
	unsigned long now)
{
	struct spartan_link_window *window;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS || !spartan_link.enabled ||
		!spartan_link_playing() || cinematic_in_progress())
	{
		return;
	}
	window = &spartan_link.windows[local_player_index];
	window->refusal = NULL;
	if (!spartan_link_game())
	{
		window->refusal = spartan_link_pro_doubles() ? L"Spartan link is off in Pro 2v2" :
			L"Spartan link: no teammates in free for all";
		window->refusal_at = now;
		if (spartan_link.log)
		{
			platform_log("spartan link: local player %d: %ls", (int)local_player_index, window->refusal);
			halo_screenshot_named("spartan_link_refusal");
		}
		return;
	}
	spartan_link_window_update(local_player_index, now);
	if (!window->open && window->count == 0)
	{
		window->refusal = L"Spartan link: no teammates alive";
		window->refusal_at = now;
		return;
	}
	window->open = !window->open;
	if (spartan_link.log)
		platform_log("spartan link: local player %d %s, %d feeds", (int)local_player_index,
			window->open ? "open" : "closed", (int)window->count);
}

/* the feed drawn this frame: one a frame of every window's, a feed whose
image was never drawn first, else the next after the last drawn */
static void spartan_link_schedule(
	void)
{
	short local_player_index;
	short feed;
	short best_local = NONE, best_feed = NONE;
	long best_distance = LONG_MAX;
	long last = spartan_link.last_local_player == NONE ? -1 :
		spartan_link.last_local_player * SPARTAN_LINK_FEEDS + spartan_link.last_feed;

	spartan_link.due_local_player = NONE;
	spartan_link.due_feed = NONE;
	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		struct spartan_link_window *window = &spartan_link.windows[local_player_index];

		if (!spartan_link_shown(local_player_index))
			continue;
		for (feed = 0; feed < window->count; feed++)
		{
			long slot = local_player_index * SPARTAN_LINK_FEEDS + feed;
			long distance;

			/* (a downed teammate's image stays as it was) */
			if (window->feeds[feed].down_at)
				continue;
			distance = slot > last ? slot - last : slot - last + MAXIMUM_LOCAL_PLAYERS * SPARTAN_LINK_FEEDS;
			if (!halo_view_drawn(SPARTAN_LINK_VIEW(local_player_index, feed)))
				distance -= MAXIMUM_LOCAL_PLAYERS * SPARTAN_LINK_FEEDS;
			if (distance < best_distance)
			{
				best_distance = distance;
				best_local = local_player_index;
				best_feed = feed;
			}
		}
	}
	spartan_link.due_local_player = best_local;
	spartan_link.due_feed = best_feed;
}

/* the window's feeds' layout: each feed's rectangle in the view's units, from
its top left (left, top, right, bottom), and the scale of the units the
design's are in. The HUD's panels are placed in the window's title-safe frame
(its window bounds, 36 of 480 below the top of a whole screen's view), so the
feeds are sized and placed across between the panels in it; but they hang
from the view's own top edge, up in the panels' line, not under the frame's
inset */
static short spartan_link_layout(
	short local_player_index,
	struct render_camera const *camera,
	real_rectangle2d *rectangles,
	real *scale)
{
	struct spartan_link_window *window = &spartan_link.windows[local_player_index];
	/* (the title-safe frame in the view's units) */
	real frame_x0 = (real)(camera->window_bounds.x0 - camera->viewport_bounds.x0);
	real width = (real)(camera->window_bounds.x1 - camera->window_bounds.x0);
	real height = (real)(camera->window_bounds.y1 - camera->window_bounds.y0);
	real feed_width, total, room, left;
	short count = window->count;
	short index;

	if (count <= 0 || width <= 0.0f || height <= 0.0f)
		return 0;
	/* (a split screen's window keeps the HUD's size, and the feeds most of
	theirs) */
	*scale = PIN(height / 480.0f * 1.5f, 0.6f, 1.0f);
	feed_width = spartan_link_widths[count - 1] * spartan_link.size * *scale;
	total = feed_width * count + SPARTAN_LINK_GAP * *scale * (count - 1);
	room = width - (SPARTAN_LINK_PANEL_LEFT + SPARTAN_LINK_PANEL_RIGHT) * *scale;
	if (total > room && room > 0.0f)
	{
		real fit = room / total;

		feed_width *= fit;
		total = room;
	}
	/* (centred between the panels) */
	left = frame_x0 + SPARTAN_LINK_PANEL_LEFT * *scale + (room - total) * 0.5f;
	for (index = 0; index < count; index++)
	{
		rectangles[index].x0 = left + index * (feed_width + SPARTAN_LINK_GAP * *scale);
		rectangles[index].x1 = rectangles[index].x0 + feed_width;
		rectangles[index].y0 = SPARTAN_LINK_TOP * *scale;
		rectangles[index].y1 = rectangles[index].y0 + feed_width * 9.0f / 16.0f;
	}
	return count;
}

/* the field of view a unit looks through: its weapon's at its zoom (as
dead_spectate.c's) */
static real spartan_link_field_of_view(
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

/* a feed's camera: the teammate's eye as their body is drawn, along their
aim; or in a seat whose camera follows the vehicle, from behind. TRUE
through their eyes, where their aim is the view's middle */
static boolean spartan_link_feed_view(
	long unit_index,
	struct render_camera *camera)
{
	struct camera_command command;
	director_perspective perspective;
	real_point3d eye;
	boolean through_eyes;

	first_person_camera_fake(unit_index, &command);
	render_interpolation_unit_camera_position(unit_index, &eye);
	through_eyes = !director_desired_perspective(unit_index, &perspective);
	camera->forward = command.forward;
	camera->up = command.up;
	if (through_eyes)
	{
		camera->position = eye;
	}
	else
	{
		struct object_datum *unit = object_get(unit_index);
		long vehicle_index = unit->object.parent_object_index;
		real radius = vehicle_index != NONE && object_try_and_get(vehicle_index) ?
			object_get(vehicle_index)->object.bounding_sphere_radius : 1.0f;
		real back = radius * 1.6f + 0.6f;

		camera->position.x = eye.x - command.forward.i * back;
		camera->position.y = eye.y - command.forward.j * back;
		camera->position.z = eye.z - command.forward.k * back + radius * 0.35f;
	}
	camera->vertical_field_of_view = 2.0f * arctangent(
		0.75f * render_camera_get_adjusted_field_of_view_tangent(
			through_eyes ? spartan_link_field_of_view(unit_index) : DEGREES_TO_RADIANS(70.0f)),
		1.0f);
	return through_eyes;
}

/* ---------- the teammate's aim */

/* what the aim's line stops at: the level, and the objects a shot would
hit or a reticle is turned by (not the weapons and equipment lying about,
nor an enemy's held weapon, which their body is behind) */
#define SPARTAN_LINK_AIM_FLAGS (FLAG(_collision_test_front_facing_surfaces_bit) | \
	FLAG(_collision_test_ignore_invisible_surfaces_bit) | FLAG(_collision_test_structure_bit) | \
	FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_bipeds_bit) | \
	FLAG(_collision_test_objects_vehicles_bit) | FLAG(_collision_test_objects_scenery_bit) | \
	FLAG(_collision_test_objects_machines_bit))
/* how far the aim's line reaches (world units) when the weapon's red reach
is shorter: aim_assist.c's shot reference */
#define SPARTAN_LINK_AIM_REACH 128.0f

/* where a point of the world lands on a feed drawn through camera into
bounds (the window's units: the camera's whole view, its vertical angle
across the bounds' height): FALSE behind the camera, or a reticle of
margin about it not wholly on the feed */
static boolean spartan_link_aim_place(
	struct render_camera const *camera,
	real_rectangle2d const *bounds,
	real margin,
	real_point3d const *point,
	real_point2d *place)
{
	real width = bounds->x1 - bounds->x0;
	real height = bounds->y1 - bounds->y0;
	real tangent_y = (real)tan(camera->vertical_field_of_view * 0.5f);
	real tangent_x = tangent_y * width / (height > 0.0f ? height : 1.0f);
	real_vector3d right, to;
	real depth;

	if (!(width > 0.0f) || !(height > 0.0f) || !(tangent_y > 0.0f))
		return FALSE;
	/* (the camera's right: forward across up, y being the world's left) */
	right.i = camera->forward.j * camera->up.k - camera->forward.k * camera->up.j;
	right.j = camera->forward.k * camera->up.i - camera->forward.i * camera->up.k;
	right.k = camera->forward.i * camera->up.j - camera->forward.j * camera->up.i;
	to.i = point->x - camera->position.x;
	to.j = point->y - camera->position.y;
	to.k = point->z - camera->position.z;
	depth = to.i * camera->forward.i + to.j * camera->forward.j + to.k * camera->forward.k;
	if (!(depth > 0.0f))
		return FALSE;
	place->x = (bounds->x0 + bounds->x1) * 0.5f +
		(to.i * right.i + to.j * right.j + to.k * right.k) / depth / tangent_x * width * 0.5f;
	place->y = (bounds->y0 + bounds->y1) * 0.5f -
		(to.i * camera->up.i + to.j * camera->up.j + to.k * camera->up.k) / depth / tangent_y * height * 0.5f;
	return place->x - margin >= bounds->x0 && place->x + margin <= bounds->x1 &&
		place->y - margin >= bounds->y0 && place->y + margin <= bounds->y1;
}

/* whether what the aim stopped at turns the teammate's reticle red, as
Halo's does for the targets its autoaim takes (aim_assist.c's): a living
biped of a team that is the teammate's enemy, not wholly camouflaged nor
one autoaim ignores, within the weapon's reach (distance and range, world
units; no range: never). A held object is its holder's */
static boolean spartan_link_aim_enemy(
	short team_index,
	long object_index,
	real distance,
	real range)
{
	struct object_datum *object = object_try_and_get(object_index);

	if (object && !TEST_FLAG(_object_mask_unit, object->object.type) && object->object.parent_object_index != NONE)
	{
		object_index = object->object.parent_object_index;
		object = object_try_and_get(object_index);
	}
	if (!object || !(range > 0.0f) || !(distance <= range) || !TEST_FLAG(_object_mask_biped, object->object.type))
		return FALSE;
	return !TEST_FLAG(object->object.damage_flags, _object_dead_bit) &&
		!TEST_FLAG(object->object.flags, _object_invisible_bit) &&
		unit_get(object_index)->unit.active_camouflage < 1.0f &&
		game_team_is_enemy(team_index, object->object.owner_team_index) &&
		!TEST_FLAG(biped_definition_get(object->definition_index)->unit.flags, _unit_ignored_by_autoaiming_bit);
}

/* the object at the root of what an object rides */
static long spartan_link_root(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);
	short depth;

	for (depth = 0; object && object->object.parent_object_index != NONE && depth < 8; depth++)
	{
		object_index = object->object.parent_object_index;
		object = object_try_and_get(object_index);
	}
	return object_index;
}

/* the first thing along the teammate's aim from origin (forward a unit
vector), out to reach, past their own unit and what it rides: TRUE with
it. A collision test only, which reads the world: no seed, no effect */
static boolean spartan_link_aim_ray(
	long unit_index,
	real_point3d const *origin,
	real_vector3d const *forward,
	real reach,
	struct collision_result *result)
{
	long root_index = spartan_link_root(unit_index);
	real_point3d from = *origin;
	boolean hit = FALSE;
	short attempt;

	if (global_current_collision_user_depth >= MAXIMUM_COLLISION_USER_STACK_DEPTH)
		return FALSE;
	global_current_collision_users[global_current_collision_user_depth++] = _collision_user_ui;
	for (attempt = 0; attempt < 4 && reach > 0.0f; attempt++)
	{
		real_vector3d vector;

		vector.i = forward->i * reach;
		vector.j = forward->j * reach;
		vector.k = forward->k * reach;
		hit = collision_test_vector(SPARTAN_LINK_AIM_FLAGS, &from, &vector, root_index, result);
		if (!hit || result->type != _collision_result_object || spartan_link_root(result->object_index) != root_index)
			break;
		/* (their own, or what they ride: on past it) */
		hit = FALSE;
		reach -= reach * result->t + 0.05f;
		from.x = result->point.x + forward->i * 0.05f;
		from.y = result->point.y + forward->j * 0.05f;
		from.z = result->point.z + forward->k * 0.05f;
	}
	--global_current_collision_user_depth;
	return hit;
}

/* ---------- the aim as this machine has it */

/* whether the teammate has an aim their own HUD draws a reticle for
(hud_weapon.c's hud_render_weapon_interface): a weapon of their own, their
seat's when it is a gunner's, or none at all on foot (the default
reticle); not in a seat with no weapon (a driver's or a passenger's, whose
camera follows the vehicle) */
static boolean spartan_link_aims(
	long unit_index)
{
	struct unit_datum *unit = unit_get(unit_index);
	struct unit_datum *parent;
	struct unit_seat const *seat;

	if (unit_inventory_get_weapon(unit_index, unit->unit.current_weapon_index) != NONE)
		return TRUE;
	if (unit->object.parent_object_index == NONE || unit->unit.parent_seat_index == NONE)
		return !unit_get_weapon_count(unit_index);
	parent = unit_try_and_get(unit->object.parent_object_index);
	if (!parent)
		return FALSE;
	seat = TAG_BLOCK_GET_ELEMENT(&unit_definition_get(parent->definition_index)->unit.seats,
		unit->unit.parent_seat_index, struct unit_seat);
	return TEST_FLAG(seat->flags, _unit_seat_gunner_bit) &&
		unit_inventory_get_weapon(unit->object.parent_object_index, parent->unit.current_weapon_index) != NONE;
}

/* how far the teammate's weapon turns the reticle red: its autoaim's reach
at its zoom (aim_assist.c's unit_get_aim_assist_parameters); 0 for none */
static real spartan_link_aim_range(
	long unit_index)
{
	long aiming_index = unit_get_aiming_unit_index(unit_index);
	struct unit_datum *unit;
	struct weapon_definition *definition;
	long weapon_index;
	short zoom_level;

	if (aiming_index == NONE || !unit_try_and_get(aiming_index))
		return 0.0f;
	unit = unit_get(aiming_index);
	weapon_index = unit_inventory_get_weapon(aiming_index, unit->unit.current_weapon_index);
	if (weapon_index == NONE)
		return 0.0f;
	definition = weapon_definition_get(weapon_get(weapon_index)->definition_index);
	zoom_level = unit_get_zoom_level(aiming_index);
	if (zoom_level == NONE && TEST_FLAG(definition->weapon.flags, _weapon_aim_assists_only_when_zoomed_bit))
		return 0.0f;
	return definition->weapon.aim_assist_parameters.autoaim_distance *
		weapon_get_zoom_magnification(weapon_index, zoom_level);
}

/* the teammate's aim as the game has it (spartan_link_update's, outside
the frame's drawing, whose objects are posed between ticks): whether they
have one to show (not down, nor in a seat without a weapon), the point it
stops at, and whether that is an enemy the reticle turns red for */
static void spartan_link_feed_aim_update(
	struct spartan_link_feed *feed)
{
	struct player_datum *teammate = player_try_and_get(feed->player_index);
	struct camera_command command;
	struct collision_result collision;
	real_vector3d forward;
	real range, reach;

	feed->aim_shown = FALSE;
	feed->aim_enemy = FALSE;
	if (!teammate || feed->down_at || !object_try_and_get_and_verify_type(feed->unit_index, _object_mask_unit) ||
		TEST_FLAG(object_get(feed->unit_index)->object.damage_flags, _object_dead_bit) ||
		!spartan_link_aims(feed->unit_index))
	{
		return;
	}
	first_person_camera_fake(feed->unit_index, &command);
	forward = command.forward;
	if (normalize3d(&forward) == 0.0f)
		return;
	range = spartan_link_aim_range(feed->unit_index);
	reach = MAX(range, SPARTAN_LINK_AIM_REACH);
	if (spartan_link_aim_ray(feed->unit_index, &command.position, &forward, reach, &collision))
	{
		feed->aim_target = collision.point;
		feed->aim_enemy = collision.type == _collision_result_object && spartan_link_aim_enemy(
			(short)teammate->team_index, collision.object_index, distance3d(&command.position, &collision.point), range);
	}
	else
	{
		point_from_line3d(&command.position, &forward, reach, &feed->aim_target);
	}
	feed->aim_shown = TRUE;
}

/* the teammate's reticle on their feed (bounds), in the window's units:
FALSE for none (no aim to show, or off the feed's picture). Through their
eyes it is the feed's middle (the feed looks along the aim); from behind
them, the point the aim stops at, seen through the feed's camera */
static boolean spartan_link_feed_aim_place(
	struct spartan_link_feed const *feed,
	real_rectangle2d const *bounds,
	real margin,
	real_point2d *place)
{
	struct render_camera camera;

	if (!feed->aim_shown || feed->down_at || !object_try_and_get_and_verify_type(feed->unit_index, _object_mask_unit))
		return FALSE;
	if (spartan_link_feed_view(feed->unit_index, &camera))
	{
		place->x = (bounds->x0 + bounds->x1) * 0.5f;
		place->y = (bounds->y0 + bounds->y1) * 0.5f;
		return TRUE;
	}
	return spartan_link_aim_place(&camera, bounds, margin, &feed->aim_target, place);
}

/* ---------- the drawing's pieces */

/* the maps' white texture, NULL where the map has none (menu_shapes.c's) */
static struct bitmap_data *spartan_link_white(
	void)
{
	struct game_globals *game_globals = scenario_get_game_globals();
	struct game_globals_rasterizer_data *rasterizer_data;

	if (!game_globals || !game_globals->rasterizer_data.count)
		return NULL;
	rasterizer_data = TAG_BLOCK_GET_ELEMENT(&game_globals->rasterizer_data, 0, struct game_globals_rasterizer_data);
	return TAG_BLOCK_GET_ELEMENT(&bitmap_group_get(rasterizer_data->default_textures[0].index)->bitmaps, 1,
		struct bitmap_data);
}

/* a quadrilateral, its corners in the window's units (clockwise from the
top left), of the white texture or (u, v from 0 to 1 over bounds) the
feed's image; its corners on the display's pixels' edges (snap), or where
they fall (a reticle's curves) */
static void spartan_link_quad_snapped(
	struct bitmap_data *white,
	real const x[4],
	real const y[4],
	pixel32 color,
	real_rectangle2d const *bounds,
	boolean snap)
{
	struct rasterizer_dynamic_screen_geometry_parameters parameters;
	struct dynamic_screen_vertex vertices[4];
	real pixels = halo_screen_pixel_scale() > 0.0f ? halo_screen_pixel_scale() : 1.0f;
	short index;

	for (index = 0; index < 4; index++)
	{
		/* (on the display's pixels' edges: where the screen's unit 0 falls
		is the Xbox's pixels' centres, menu_shapes.c) */
		vertices[index].position.x = (snap ? floorf(x[index] * pixels + 0.5f) / pixels : x[index]) + 0.5f;
		vertices[index].position.y = (snap ? floorf(y[index] * pixels + 0.5f) / pixels : y[index]) + 0.5f;
		vertices[index].texture_coordinates.x = bounds ? (x[index] - bounds->x0) / (bounds->x1 - bounds->x0) : 0.0f;
		vertices[index].texture_coordinates.y = bounds ? (y[index] - bounds->y0) / (bounds->y1 - bounds->y0) : 0.0f;
		vertices[index].color = color;
	}
	csmemset(&parameters, 0, sizeof(parameters));
	parameters.map[0] = white;
	parameters.map_texture_scale[0].i = parameters.map_texture_scale[0].j = 1.0f;
	parameters.map_scale[0].i = parameters.map_scale[0].j = 1.0f;
	rasterizer_globals.current_lock_operation = _rasterizer_lock_cinematics;
	rasterizer_psuedo_dynamic_screen_quad_draw(&parameters, vertices);
	rasterizer_globals.current_lock_operation = _rasterizer_lock_none;
}

static void spartan_link_quad(
	struct bitmap_data *white,
	real const x[4],
	real const y[4],
	pixel32 color,
	real_rectangle2d const *bounds)
{
	spartan_link_quad_snapped(white, x, y, color, bounds, TRUE);
}

static void spartan_link_rectangle(
	struct bitmap_data *white,
	real x0,
	real y0,
	real x1,
	real y1,
	pixel32 color)
{
	real x[4], y[4];

	if (x1 <= x0 || y1 <= y0)
		return;
	x[0] = x0; y[0] = y0;
	x[1] = x1; y[1] = y0;
	x[2] = x1; y[2] = y1;
	x[3] = x0; y[3] = y1;
	spartan_link_quad(white, x, y, color, NULL);
}

/* a colour of the HUD's text (its messages': hud_get_text_color), at alpha */
static pixel32 spartan_link_color(
	real alpha,
	real brightness)
{
	real_argb_color text_color;
	real_argb_color const *text = hud_get_text_color(&text_color);
	real_argb_color color;

	color.alpha = alpha;
	color.red = PIN(text->red * brightness, 0.0f, 1.0f);
	color.green = PIN(text->green * brightness, 0.0f, 1.0f);
	color.blue = PIN(text->blue * brightness, 0.0f, 1.0f);
	return real_argb_color_to_pixel32(&color);
}

/* a line of text in the HUD's font, its top at y in the window's units,
scaled: from x (justification 0), ending at x + width (1), or centred over
width (2) */
static void spartan_link_text(
	wchar_t const *text,
	real x,
	real y,
	real width,
	short justification,
	real text_scale,
	real_argb_color const *color)
{
	long font_index = hud_get_font_index();
	struct font_header *font;
	rectangle2d bounds, text_bounds, cursor_bounds;
	real height, text_width, left;

	if (font_index == NONE || !text[0])
		return;
	font = font_definition_get(font_index);
	height = (real)(font->ascending_height + font->descending_height);
	/* (measured unscaled, left-justified; then drawn from where it starts,
	scaled about that corner) */
	draw_string_set_draw_mode(font_index, NONE, 0, 0, color);
	bounds.x0 = 0;
	bounds.y0 = 0;
	bounds.x1 = 2000;
	bounds.y1 = (short)ceilf(height);
	draw_unicode_string_compute_bounds(&bounds, text, &text_bounds, &cursor_bounds);
	text_width = (real)(text_bounds.x1 - text_bounds.x0) * text_scale;
	left = justification == 1 ? x + width - text_width : justification == 2 ? x + (width - text_width) * 0.5f : x;
	bounds.x0 = (short)floorf(left);
	bounds.y0 = (short)floorf(y);
	bounds.x1 = (short)(bounds.x0 + 2000);
	bounds.y1 = (short)ceilf(y + height);
	rasterizer_text_set_scale(text_scale, (real)bounds.x0, (real)bounds.y0);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, text);
	rasterizer_text_set_scale(1.0f, 0.0f, 0.0f);
}

/* the frame about a feed: its image (or, never drawn, dark glass), a tint,
an edge of the HUD's colour, its top corners cut in */
static void spartan_link_draw_feed(
	struct bitmap_data *white,
	short local_player_index,
	short index,
	real_rectangle2d const *bounds,
	real scale)
{
	struct spartan_link_feed const *feed = &spartan_link.windows[local_player_index].feeds[index];
	int view = SPARTAN_LINK_VIEW(local_player_index, index);
	real slant = (bounds->y1 - bounds->y0) * SPARTAN_LINK_SLANT;
	real edge = MAX(1.0f / (halo_screen_pixel_scale() > 0.0f ? halo_screen_pixel_scale() : 1.0f), 0.75f * scale);
	real x[4], y[4];
	pixel32 edge_color = spartan_link_color(0.9f, 1.0f);

	/* the image, tinted toward the HUD's blue (a down teammate's darkened) */
	x[0] = bounds->x0 + slant; y[0] = bounds->y0;
	x[1] = bounds->x1 - slant; y[1] = bounds->y0;
	x[2] = bounds->x1; y[2] = bounds->y1;
	x[3] = bounds->x0; y[3] = bounds->y1;
	if (halo_view_drawn(view))
	{
		halo_view_sample(view);
		spartan_link_quad(white, x, y, feed->down_at ? 0xFF505868UL : 0xFFDCEBFFUL, bounds);
		halo_view_sample(0);
		spartan_link_quad(white, x, y, spartan_link_color(0.10f, 1.0f), NULL);
	}
	else
	{
		spartan_link_quad(white, x, y, 0xC0081018UL, NULL);
	}
	/* the edge: top and bottom, and the angled ends */
	spartan_link_rectangle(white, bounds->x0 + slant, bounds->y0, bounds->x1 - slant, bounds->y0 + edge, edge_color);
	spartan_link_rectangle(white, bounds->x0, bounds->y1 - edge, bounds->x1, bounds->y1, edge_color);
	x[0] = bounds->x0 + slant; y[0] = bounds->y0;
	x[1] = bounds->x0 + slant + edge; y[1] = bounds->y0;
	x[2] = bounds->x0 + edge; y[2] = bounds->y1;
	x[3] = bounds->x0; y[3] = bounds->y1;
	spartan_link_quad(white, x, y, edge_color, NULL);
	x[0] = bounds->x1 - slant - edge; y[0] = bounds->y0;
	x[1] = bounds->x1 - slant; y[1] = bounds->y0;
	x[2] = bounds->x1; y[2] = bounds->y1;
	x[3] = bounds->x1 - edge; y[3] = bounds->y1;
	spartan_link_quad(white, x, y, edge_color, NULL);
}

/* the reticle's radius on a feed: a twentieth of its height, three of the
display's pixels at least */
static real spartan_link_reticle_radius(
	real_rectangle2d const *bounds)
{
	real pixels = halo_screen_pixel_scale() > 0.0f ? halo_screen_pixel_scale() : 1.0f;

	return MAX((bounds->y1 - bounds->y0) * SPARTAN_LINK_RETICLE_SIZE, 3.0f / pixels);
}

/* the teammate's reticle at place: a dot and a thin ring about it, in the
HUD's blue, or red on an enemy */
static void spartan_link_draw_reticle(
	struct bitmap_data *white,
	real_point2d const *place,
	real radius,
	boolean enemy)
{
	real pixels = halo_screen_pixel_scale() > 0.0f ? halo_screen_pixel_scale() : 1.0f;
	real thickness = MAX(radius * 0.18f, 1.0f / pixels);
	real dot = MAX(radius * 0.22f, 1.0f / pixels);
	pixel32 color = enemy ? SPARTAN_LINK_RETICLE_RED : spartan_link_color(0.95f, 1.0f);
	real x[4], y[4];
	short index;

	for (index = 0; index < SPARTAN_LINK_RETICLE_SEGMENTS; index++)
	{
		real angle0 = (real)index * 2.0f * _pi / SPARTAN_LINK_RETICLE_SEGMENTS;
		real angle1 = (real)(index + 1) * 2.0f * _pi / SPARTAN_LINK_RETICLE_SEGMENTS;

		x[0] = place->x + cosf(angle0) * radius; y[0] = place->y + sinf(angle0) * radius;
		x[1] = place->x + cosf(angle1) * radius; y[1] = place->y + sinf(angle1) * radius;
		x[2] = place->x + cosf(angle1) * (radius - thickness); y[2] = place->y + sinf(angle1) * (radius - thickness);
		x[3] = place->x + cosf(angle0) * (radius - thickness); y[3] = place->y + sinf(angle0) * (radius - thickness);
		spartan_link_quad_snapped(white, x, y, color, NULL, FALSE);
	}
	/* (the dot: an octagon, its middle band and the two ends) */
	{
		real side = dot * 0.4142f;

		spartan_link_rectangle(white, place->x - dot, place->y - side, place->x + dot, place->y + side, color);
		x[0] = place->x - side; y[0] = place->y - dot;
		x[1] = place->x + side; y[1] = place->y - dot;
		x[2] = place->x + dot; y[2] = place->y - side;
		x[3] = place->x - dot; y[3] = place->y - side;
		spartan_link_quad_snapped(white, x, y, color, NULL, FALSE);
		x[0] = place->x - dot; y[0] = place->y + side;
		x[1] = place->x + dot; y[1] = place->y + side;
		x[2] = place->x + side; y[2] = place->y + dot;
		x[3] = place->x - side; y[3] = place->y + dot;
		spartan_link_quad_snapped(white, x, y, color, NULL, FALSE);
	}
}

/* the HUD's health meter's colour at a vitality, from the unit's own HUD
(hud_unit.c's: its full colour at or above the meter's upper cutoff, its
empty colour at or below the lower, its middle colour between), and while
flashing (since flash_at) the meter's background's flash, as the HUD's
health panel flashes while the shield is down; the HUD's text colour where
the unit has none */
static pixel32 spartan_link_health_color(
	long unit_index,
	real vitality,
	unsigned long flash_at)
{
	struct unit_definition *definition = unit_definition_get(unit_get(unit_index)->definition_index);
	long hud_index = unit_definition_get_active_hud_index(definition, FALSE);
	struct unit_hud_interface_definition const *hud;
	struct hud_color_definition const *flash;
	real_argb_color color, flash_color;
	real fraction = 0.0f;

	if (hud_index == NONE)
		return spartan_link_color(0.95f, 1.0f);
	hud = unit_hud_interface_definition_get(hud_index);
	pixel32_to_real_argb_color(
		vitality >= hud->health_meter.health_extras.max_cutoff ? hud->health_meter.meter.max_color :
		vitality <= hud->health_meter.health_extras.min_cutoff ? hud->health_meter.meter.min_color :
		hud->health_meter.health_extras.mid_color, &color);
	color.alpha = 0.95f;
	/* (the flash: get_flash_color's pulse, its period's first flashes) */
	flash = &hud->health_meter.background.colors;
	if (flash_at && flash->flash_period > 0.0f && flash->flash_length > 0.0f)
	{
		real phase = fmodf((real)(system_milliseconds() - flash_at) * 0.001f, flash->flash_period);
		real cycle = flash->flash_delay + flash->flash_length;

		if (phase < flash->number_of_flashes * cycle)
		{
			phase = fmodf(phase, cycle);
			if (phase < flash->flash_length)
				fraction = sqrtf(PIN(1.0f - (cosf(phase / flash->flash_length * 2.0f * _pi) + 1.0f) * 0.5f, 0.0f, 1.0f));
		}
	}
	pixel32_to_real_argb_color(flash->flash_color, &flash_color);
	color.red += (flash_color.red - color.red) * fraction;
	color.green += (flash_color.green - color.green) * fraction;
	color.blue += (flash_color.blue - color.blue) * fraction;
	return real_argb_color_to_pixel32(&color);
}

/* a line's width in the HUD's font, unscaled (spartan_link_text's units) */
static real spartan_link_text_width(
	wchar_t const *text)
{
	long font_index = hud_get_font_index();
	rectangle2d bounds, text_bounds, cursor_bounds;

	if (font_index == NONE || !text[0])
		return 0.0f;
	draw_string_set_draw_mode(font_index, NONE, 0, 0, global_real_argb_white);
	bounds.x0 = 0;
	bounds.y0 = 0;
	bounds.x1 = 2000;
	bounds.y1 = 100;
	draw_unicode_string_compute_bounds(&bounds, text, &text_bounds, &cursor_bounds);
	return (real)(text_bounds.x1 - text_bounds.x0);
}

/* under a feed: the teammate's name, thin bars of their shield and under it
their health, and how far away they are (the name cut short, and the bars
left out, where the feed is too narrow for them all) */
static void spartan_link_draw_row(
	struct bitmap_data *white,
	short local_player_index,
	short index,
	real_rectangle2d const *bounds,
	real scale)
{
	struct spartan_link_feed *feed = &spartan_link.windows[local_player_index].feeds[index];
	struct player_datum *teammate = player_try_and_get(feed->player_index);
	long self = local_player_get_player_index(local_player_index);
	real pixel = 1.0f / (halo_screen_pixel_scale() > 0.0f ? halo_screen_pixel_scale() : 1.0f);
	real_argb_color text_color;
	real width = bounds->x1 - bounds->x0;
	real top = bounds->y1 + 1.5f * scale;
	real margin = 2.0f * scale;
	/* (the text as large as the design's at a feed's width of four, smaller
	in a narrower feed, to a point) */
	real text_scale = 0.6f * MAX(scale, 0.9f) * MAX(spartan_link.size, 0.9f) *
		PIN(width / (spartan_link_widths[SPARTAN_LINK_FEEDS - 1] * scale), 0.75f, 1.0f);
	real shield = 0.0f, health = 0.0f;
	real bar_height = MAX(2.0f * scale, pixel);
	real health_height = MAX(1.5f * scale, pixel);
	real name_width, distance_width, bar_x0, bar_x1, bar_y, health_y;
	wchar_t name[16];
	wchar_t distance[16];
	short length;

	if (!teammate)
		return;
	hud_get_text_color(&text_color);
	ustrncpy(name, teammate->name, NUMBEROF(name) - 1);
	name[NUMBEROF(name) - 1] = 0;
	distance[0] = 0;
	if (feed->down_at)
	{
		usnprintf(distance, NUMBEROF(distance) - 1, L"DOWN");
	}
	else if (self != NONE && player_get(self)->unit_index != NONE &&
		object_try_and_get(feed->unit_index))
	{
		real_point3d here, there;

		object_get_origin(player_get(self)->unit_index, &here);
		object_get_origin(feed->unit_index, &there);
		usnprintf(distance, NUMBEROF(distance) - 1, L"%ldm", (long)floorf(distance3d(&here, &there) * 3.048f + 0.5f));
		shield = object_get(feed->unit_index)->object.shield_vitality;
		health = object_get(feed->unit_index)->object.body_vitality;
	}
	/* (the health flashing while the shield is down, as the HUD's: from
	when it went down) */
	if (!feed->down_at && object_try_and_get(feed->unit_index) &&
		TEST_FLAG(object_get(feed->unit_index)->object.damage_flags, _object_shield_depleted_bit))
	{
		if (!feed->health_flash_at)
			feed->health_flash_at = system_milliseconds() ? system_milliseconds() : 1;
	}
	else
	{
		feed->health_flash_at = 0;
	}
	distance[NUMBEROF(distance) - 1] = 0;
	distance_width = spartan_link_text_width(distance) * text_scale;
	/* (the name as long as fits beside the distance) */
	for (length = (short)ustrlen(name); length > 0; length--)
	{
		name[length] = 0;
		name_width = spartan_link_text_width(name) * text_scale;
		if (name_width + distance_width + 3.0f * margin <= width)
			break;
	}
	name_width = spartan_link_text_width(name) * text_scale;
	text_color.alpha = 1.0f;
	spartan_link_text(name, bounds->x0 + margin, top, name_width, 0, text_scale, &text_color);
	spartan_link_text(distance, bounds->x1 - margin - distance_width, top, distance_width, 0, text_scale,
		&text_color);
	/* (the shield: a thin bar between them, where there is room for one;
	the health a thinner one under it, the two about the text's middle) */
	bar_x0 = bounds->x0 + margin * 2.0f + name_width;
	bar_x1 = bounds->x1 - margin * 2.0f - distance_width;
	bar_y = top + SPARTAN_LINK_ROW * scale * 0.30f;
	health_y = bar_y + bar_height + MAX(1.25f * scale, 2.0f * pixel);
	if (bar_x1 - bar_x0 >= 6.0f * scale)
	{
		pixel32 health_color = object_try_and_get_and_verify_type(feed->unit_index, _object_mask_unit) ?
			spartan_link_health_color(feed->unit_index, health, feed->health_flash_at) : spartan_link_color(0.95f, 1.0f);

		shield = PIN(shield, 0.0f, 1.0f);
		health = PIN(health, 0.0f, 1.0f);
		spartan_link_rectangle(white, bar_x0, bar_y, bar_x1, bar_y + bar_height, spartan_link_color(0.35f, 0.6f));
		spartan_link_rectangle(white, bar_x0, bar_y, bar_x0 + (bar_x1 - bar_x0) * shield, bar_y + bar_height,
			spartan_link_color(0.95f, 1.0f));
		spartan_link_rectangle(white, bar_x0, health_y, bar_x1, health_y + health_height,
			(health_color & 0x00FFFFFFUL) | 0x40000000UL);
		spartan_link_rectangle(white, bar_x0, health_y, bar_x0 + (bar_x1 - bar_x0) * health, health_y + health_height,
			health_color);
	}
}

/* ---------- public code */

void spartan_link_update(
	void)
{
	unsigned long now = system_milliseconds();
	short local_player_index;
	boolean key;
	short port;

	spartan_link_read();
	/* (a new game: every link closed, as each game starts) */
	if (!spartan_link_playing() || game_time_get() < spartan_link.last_game_time)
	{
		if (spartan_link.in_game)
		{
			short view;

			csmemset(spartan_link.windows, 0, sizeof(spartan_link.windows));
			for (view = 1; view <= MAXIMUM_LOCAL_PLAYERS * SPARTAN_LINK_FEEDS; view++)
			{
				if (spartan_link.views_used & (1UL << view))
					halo_view_release(view);
			}
			spartan_link.views_used = 0;
		}
		spartan_link.in_game = FALSE;
	}
	if (spartan_link_playing())
	{
		if (!spartan_link.in_game)
		{
			csmemset(spartan_link.windows, 0, sizeof(spartan_link.windows));
			spartan_link.last_local_player = NONE;
			spartan_link.last_feed = NONE;
			/* (debug.spartan_link_open: open from the start, for the tests) */
			spartan_link.debug_open = config_boolean("debug.spartan_link_open");
		}
		spartan_link.in_game = TRUE;
		spartan_link.last_game_time = game_time_get();
	}
	/* the presses: a controller's (the stock layouts' D-pad Down, each
	port's own player's), the keyboard's (the first player's) */
	for (port = 0; port < MAXIMUM_LOCAL_PLAYERS; port++)
	{
		if (halo_spartan_link_pad_pressed(port))
			spartan_link_toggle(controller_remap_local_player(port), now);
	}
	key = (halo_keyboard_actions(0) & FLAG(HALO_KEYBOARD_SPARTAN_LINK)) != 0;
	if (key && !spartan_link.key_held && !console_is_active() && !network_chat_typing())
	{
		short first = controller_remap_local_player(0);

		if (first == NONE)
			first = local_player_get_next(NONE);
		if (first != NONE && !ui_widgets_active_for_local_player(first))
			spartan_link_toggle(first, now);
	}
	spartan_link.key_held = key;
	if (!spartan_link.in_game)
		return;
	/* (debug.spartan_link_log: each tick, every player's eye and aim as this
	machine has them, which another machine's feeds are compared with) */
	if (spartan_link.log && game_time_get() != spartan_link.logged_tick)
	{
		struct data_iterator iterator;
		struct player_datum *player;

		spartan_link.logged_tick = game_time_get();
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			struct camera_command command;

			if (player->unit_index == NONE || !object_try_and_get_and_verify_type(player->unit_index, _object_mask_unit))
				continue;
			first_person_camera_fake(player->unit_index, &command);
			platform_log("spartan link player: %ld %ld %.4f %.4f %.4f %.5f %.5f %.5f", game_time_get(),
				(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index), command.position.x, command.position.y,
				command.position.z, command.forward.i, command.forward.j, command.forward.k);
		}
	}
	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		struct spartan_link_window *window = &spartan_link.windows[local_player_index];

		if ((window->open || spartan_link.debug_open) && spartan_link_game())
			spartan_link_window_update(local_player_index, now);
		else if (window->open && !spartan_link_game())
			window->open = FALSE;
		/* (the teammates' aims, while the link is shown) */
		if (spartan_link_shown(local_player_index))
		{
			short feed;

			for (feed = 0; feed < window->count; feed++)
				spartan_link_feed_aim_update(&window->feeds[feed]);
		}
	}
	spartan_link_schedule();
}

unsigned char spartan_link_pad_open(
	short port)
{
	short local_player_index;

	if (!spartan_link.enabled || !spartan_link_playing() || cinematic_in_progress() || !controller_remap_playing(port))
		return FALSE;
	local_player_index = controller_remap_local_player(port);
	return local_player_index != NONE && !ui_widgets_active_for_local_player(local_player_index) &&
		(game_engine_running() || network_coop_active() || local_player_count() > 1);
}

boolean spartan_link_feed_begin(
	short local_player_index,
	struct render_camera *camera)
{
	struct spartan_link_window *window;
	struct spartan_link_feed *feed;
	struct player_datum *teammate;
	real_rectangle2d rectangles[SPARTAN_LINK_FEEDS];
	real scale;
	boolean through_eyes;
	unsigned long width, height;
	int view;

	if (local_player_index != spartan_link.due_local_player || spartan_link.due_feed == NONE ||
		!spartan_link_shown(local_player_index))
	{
		return FALSE;
	}
	window = &spartan_link.windows[local_player_index];
	if (spartan_link.due_feed >= window->count ||
		spartan_link_layout(local_player_index, camera, rectangles, &scale) <= spartan_link.due_feed)
	{
		return FALSE;
	}
	feed = &window->feeds[spartan_link.due_feed];
	teammate = player_try_and_get(feed->player_index);
	if (!teammate || feed->down_at || !object_try_and_get_and_verify_type(feed->unit_index, _object_mask_unit))
		return FALSE;
	/* (the next frame's is another's) */
	spartan_link.last_local_player = spartan_link.due_local_player;
	spartan_link.last_feed = spartan_link.due_feed;
	spartan_link.due_local_player = NONE;
	spartan_link.due_feed = NONE;

	through_eyes = spartan_link_feed_view(feed->unit_index, camera);
	camera->mirrored = FALSE;
	camera->z_near = rasterizer_globals.near_clip_distance;
	camera->z_far = render_far_clip_distance(local_player_index);
	/* its window: its size on the screen, from the screen's top left */
	width = (unsigned long)floorf(rectangles[spartan_link.last_feed].x1 - rectangles[spartan_link.last_feed].x0 + 0.5f);
	height = (unsigned long)floorf(rectangles[spartan_link.last_feed].y1 - rectangles[spartan_link.last_feed].y0 + 0.5f);
	if (width < 8 || height < 8)
		return FALSE;
	camera->viewport_bounds.x0 = 0;
	camera->viewport_bounds.y0 = 0;
	camera->viewport_bounds.x1 = (short)width;
	camera->viewport_bounds.y1 = (short)height;
	camera->window_bounds = camera->viewport_bounds;
	feed->width = width;
	feed->height = height;

	view = SPARTAN_LINK_VIEW(local_player_index, spartan_link.last_feed);
	spartan_link.views_used |= 1UL << view;
	spartan_link.drawing = TRUE;
	spartan_link.hidden_unit = through_eyes ? feed->unit_index : NONE;
	spartan_link.posed_unit = through_eyes && object_get(feed->unit_index)->object.parent_object_index == NONE ?
		feed->unit_index : NONE;
	halo_view_begin(view, width, height);
	/* (what the view's drawing sets of the lights, put back after it) */
	lights_port_keep(TRUE);
	if (spartan_link.log)
	{
		platform_log("spartan link: %ld %d %ld %d %.4f %.4f %.4f %.5f %.5f %.5f %.4f %.3f %.3f", game_time_get(),
			(int)local_player_index, (long)DATUM_INDEX_TO_ABSOLUTE_INDEX(feed->player_index), through_eyes ? 1 : 0,
			camera->position.x, camera->position.y, camera->position.z, camera->forward.i, camera->forward.j,
			camera->forward.k, camera->vertical_field_of_view, render_interpolation_fraction(),
			render_interpolation_first_person_feed_phase());
	}
	return TRUE;
}

void spartan_link_feed_end(
	void)
{
	halo_view_end();
	lights_port_keep(FALSE);
	spartan_link.drawing = FALSE;
	spartan_link.hidden_unit = NONE;
	spartan_link.posed_unit = NONE;
}

boolean spartan_link_drawing(
	void)
{
	return spartan_link.drawing;
}

long spartan_link_hidden_unit(
	void)
{
	return spartan_link.drawing ? spartan_link.hidden_unit : NONE;
}

long spartan_link_posed_unit(
	void)
{
	return spartan_link.drawing ? spartan_link.posed_unit : NONE;
}

/* (debug.spartan_link_log: a feed's first-person weapon's state as it
changes, first_person_weapons.c's feeds' slots) */
void spartan_link_feed_state_log(
	long unit_index,
	char const *state,
	real firing_push_back_velocity)
{
	if (spartan_link.log)
	{
		platform_log("spartan link feed state: %ld %lx %s %.3f", game_time_get(), (unsigned long)unit_index, state,
			firing_push_back_velocity);
	}
}

short spartan_link_linked_units(
	long *unit_indices,
	short maximum_count)
{
	short count = 0;
	short local_player_index;

	for (local_player_index = 0; local_player_index < MAXIMUM_LOCAL_PLAYERS; local_player_index++)
	{
		struct spartan_link_window *window = &spartan_link.windows[local_player_index];
		short feed;

		if (!spartan_link.in_game || !spartan_link_shown(local_player_index))
			continue;
		for (feed = 0; feed < window->count; feed++)
		{
			long unit_index = window->feeds[feed].unit_index;
			short index;

			if (window->feeds[feed].down_at || unit_index == NONE)
				continue;
			for (index = 0; index < count && unit_indices[index] != unit_index; index++)
				;
			if (index == count && count < maximum_count)
				unit_indices[count++] = unit_index;
		}
	}
	return count;
}

void spartan_link_draw(
	short local_player_index)
{
	struct spartan_link_window *window;
	struct bitmap_data *white;
	real_rectangle2d rectangles[SPARTAN_LINK_FEEDS];
	real scale;
	short count;
	short index;

	if (local_player_index < 0 || local_player_index >= MAXIMUM_LOCAL_PLAYERS || spartan_link.drawing)
		return;
	window = &spartan_link.windows[local_player_index];
	white = NULL;
	/* (the HUD's coordinates are the view's own, from its top left: a split
	screen's lower window's too) */
	if (window->refusal && system_milliseconds() - window->refusal_at < SPARTAN_LINK_REFUSAL_MILLISECONDS)
	{
		real_argb_color color;
		rectangle2d const *frame = &render.camera.window_bounds;
		real height = (real)(frame->y1 - frame->y0);
		real line_scale = PIN(height / 480.0f * 1.5f, 0.6f, 1.0f);

		hud_get_text_color(&color);
		color.alpha = 1.0f;
		spartan_link_text(window->refusal, (real)(frame->x0 - render.camera.viewport_bounds.x0),
			SPARTAN_LINK_TOP * line_scale, (real)(frame->x1 - frame->x0), 2, 0.8f * line_scale, &color);
	}
	if (!spartan_link_shown(local_player_index))
		return;
	white = spartan_link_white();
	if (!white)
		return;
	count = spartan_link_layout(local_player_index, &render.camera, rectangles, &scale);
	for (index = 0; index < count; index++)
	{
		spartan_link_draw_feed(white, local_player_index, index, &rectangles[index], scale);
		spartan_link_draw_row(white, local_player_index, index, &rectangles[index], scale);
		/* (the teammate's reticle, on their feed's picture) */
		if (halo_view_drawn(SPARTAN_LINK_VIEW(local_player_index, index)))
		{
			struct spartan_link_feed *feed = &window->feeds[index];
			real radius = spartan_link_reticle_radius(&rectangles[index]);
			real_point2d place;
			boolean enemy = feed->aim_enemy;
			boolean shown = spartan_link_feed_aim_place(feed, &rectangles[index], radius, &place);

			if (shown)
				spartan_link_draw_reticle(white, &place, radius, enemy);
			/* (debug.spartan_link_log: the reticle's and the health's
			changes, and the first red's frame saved) */
			if (spartan_link.log)
			{
				short state = !shown ? 0 : enemy ? 2 : 1;
				real health = object_try_and_get(feed->unit_index) ?
					object_get(feed->unit_index)->object.body_vitality : 0.0f;

				if (state != feed->logged_state || fabsf(health - feed->logged_health) >= 0.05f)
				{
					platform_log("spartan link reticle: %ld local %d feed %d (player %ld) %s at %.1f %.1f of %.1f-%.1f "
						"%.1f-%.1f, health %.2f", game_time_get(), (int)local_player_index, (int)index,
						(long)DATUM_INDEX_TO_ABSOLUTE_INDEX(feed->player_index),
						state == 2 ? "red" : state == 1 ? "blue" : "none", place.x, place.y, rectangles[index].x0,
						rectangles[index].x1, rectangles[index].y0, rectangles[index].y1, health);
					feed->logged_state = state;
					feed->logged_health = health;
				}
				if (state == 2 && !spartan_link.red_saved)
				{
					spartan_link.red_saved = TRUE;
					halo_screenshot_named("spartan_link_red");
				}
			}
		}
	}
}
