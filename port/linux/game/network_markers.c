/*
NETWORK_MARKERS.C

Pings: a player marks what they aim at for their allies, with Halo's own
nav point (the campaign's arrow and its distance in meters, hud_nav_points.c's
custom_render_nav_point), in the player's colour with their name above it,
seen through walls (the arrow's occluded look behind them), for
MARKER_SHOWN_MILLISECONDS and then faded out. A player has one ping at a
time: a new one takes the place of the last, and a ping of the same spot or
thing again takes it away. The press is controls.ping (the middle mouse
button) or, on a controller, PING in its Button Remapping, which with the
stock layouts is D-pad Right (xinput_sdl.c then keeps that direction's
strafe from the game while pings can be sent).

What the ping is on: the first of the units, vehicles, weapons and equipment
the aim passes near (within MARKER_PICK_TANGENT of the line, before what it
hits), followed as it moves and drawn over it; else the point the aim hits
(the level, scenery), within MARKER_RANGE. A weapon in a unit's hands marks
the unit.

Who sees it: in the campaign (co-op, split screen) every player; in a
multiplayer game with teams, the player's team; in a game without teams,
nobody (pings are not sent), nor in a game of one player alone on this
machine (markers_available). A network game's are OpenH1's own, in an OpenH1
game alone (network_standard_agreed_rate: none in a Legacy game, nor to an
OpenCE machine of a mixed one, network_standard_message_goes_to): a client
sends its player's ping to the host (_distributed_message_marker_up,
reliably), which takes it only over the machine's own stream, from one of
that machine's players with a living unit, at most MARKER_BURST at once and
one each MARKER_REFILL_MILLISECONDS after, a point inside the world and no
further than MARKER_HOST_RANGE from the player, an object that is there and
of the kinds above (else the point). The host decides whether it clears the
player's last, and sends it (or the clearing) on to every OpenH1 machine
with a player who sees it (_distributed_message_marker_down, reliably),
which shows it as the host sent it. The host's network.pings (on by default:
Server Setup's PINGS) goes to its clients with it each MARKER_CONFIG_MILLISECONDS
and as it changes; a client without it (a host before pings) sends none.
Each machine plays the UI's cursor sound as a ping its players see comes.

Mutes, as the text chat's and the voice's: a player this machine muted (the
scoreboard's Mute voice, chat and pings: network_voice_machine_muted) has
their pings neither drawn nor heard here, which no one else is told
(markers_shown_to; the host still sends them). The host's mute for everyone
(the chat's /mute, the scoreboard's Mute for everyone, Host Controls' MUTE:
network_chat_host_mute_minutes) refuses the muted player's pings, as it
relays none of their voice and takes none of their lines: no machine shows
them, their own neither.

Nothing of it is the simulation's: the pings are kept by the machine's
clock, not the game's ticks, and the recorded and replayed game is the same
without them. The pure parts (network_markers_point_valid,
network_markers_same_spot, markers_object_drawn_point) are
tools/harness/tests/test_markers.py's; the name over the arrow
(markers_draw_name) is test_hud_marks.py's.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "memory/data.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "camera/observer.h"
#include "cutscene/cinematics.h"
#include "interface/hud.h"
#include "interface/hud_draw.h"
#include "interface/hud_messaging.h"
#include "interface/ui_widget.h"
#include "main/console.h"
#include "math/real_math.h"
#include "objects/objects.h"
#include "physics/collisions.h"
#include "physics/collision_usage.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "saved games/player_profile.h"
#include "sound/game_sound.h"
#include "sound/sound_definitions.h"
#include "tag_files/tag_groups.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "units/units.h"
#include "controller_remap.h"
#include "halo_keyboard.h"
#include "network_distributed.h"
#include "network_standard.h"
#include "network_coop.h"
#include "network_chat.h"
#include "network_voice.h"
#include "network_markers.h"

/* the platform layer's */
unsigned long system_milliseconds(void);
void platform_log(char const *format, ...);
/* port_config.c's */
int config_boolean(const char *name);
unsigned long config_changes(void);
/* network_connection.c's */
boolean network_connection_last_read_was_unreliable(void);
/* xinput_sdl.c's: the stock layout's ping (D-pad Right) pressed on the
port's controller since it was last asked */
int halo_ping_pad_pressed(short port);
/* hud_nav_points.c's: an arrow by its name (NULL: the first), NONE for none */
short hud_find_nav_point_quietly(char const *name);
boolean hud_render_nav_point_colored(short local_player_index, real_point3d const *position, short nav_index,
	short waypoint_type, real_rgb_color const *color, real alpha, real_point2d *drawn_position, real *half_height,
	short *drawn_type);
short hud_get_nav_point_render_type(short local_player_index, real_point3d const *head, real_point3d const *position,
	long reference_object_index);

/* ---------- constants */

enum
{
	/* the message's format: a machine takes its own alone */
	MARKER_FORMAT = 1,
	/* a ping shown whole, then faded out */
	MARKER_SHOWN_MILLISECONDS = 8000,
	MARKER_FADE_MILLISECONDS = 1500,
	/* the pings a player sends at once, and the time they gain another in
	(the host's count, and the player's own machine's) */
	MARKER_BURST = 3,
	MARKER_REFILL_MILLISECONDS = 1200,
	/* the host: its settings sent again this often (to a machine that
	joined since) */
	MARKER_CONFIG_MILLISECONDS = 3000,
	/* the host: the refusals it logs in a game */
	MARKER_REFUSALS_LOGGED = 32,
	MAXIMUM_MARKER_PLAYERS = MAXIMUM_TRACKED_PLAYERS,
	MAXIMUM_MARKER_MACHINES = HALO_PORT_MAXIMUM_NETWORK_MACHINES,
	/* a ping's object's kinds */
	MARKER_OBJECT_MASK = _object_mask_unit | FLAG(_object_type_weapon) | FLAG(_object_type_equipment),
	/* the aim's line: the level, its media skipped, invisible surfaces
	skipped, and those objects and scenery and machines */
	MARKER_AIM_FLAGS = FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_ignore_invisible_surfaces_bit) | FLAG(_collision_test_structure_bit) |
		FLAG(_collision_test_objects_bit) | FLAG(_collision_test_objects_bipeds_bit) |
		FLAG(_collision_test_objects_vehicles_bit) | FLAG(_collision_test_objects_weapons_bit) |
		FLAG(_collision_test_objects_equipment_bit) | FLAG(_collision_test_objects_scenery_bit) |
		FLAG(_collision_test_objects_machines_bit),
};

/* the message's kinds */
enum
{
	/* a ping of a point, and of an object (followed) */
	_marker_kind_point,
	_marker_kind_object,
	/* (down) the player's ping taken away */
	_marker_kind_clear,
	/* (down) the host's settings: flags, _marker_config_* */
	_marker_kind_config,
	NUMBER_OF_MARKER_KINDS
};

/* the host's settings' flags */
enum
{
	/* its players may ping */
	_marker_config_on_bit,
};

/* how far the aim reaches (world units: a little under 1000 m, what the
arrow's distance shows) */
#define MARKER_RANGE 320.0f
/* the host: how far from the player's unit a ping may be */
#define MARKER_HOST_RANGE 400.0f
/* the world's bounds, each way (a point past them is not a level's) */
#define MARKER_WORLD_LIMIT 20000.0f
/* a ping this near the player's last (world units: about 5 m), or of its
object, takes it away */
#define MARKER_SAME_SPOT 1.6f
/* an object the aim passes this near (a tangent: about 2 degrees), and its
bounding sphere, is what is aimed at */
#define MARKER_PICK_TANGENT 0.035f
/* the arrow's middle (it is drawn about it) over a point (world units: 0.3
m), just over a biped's head, and over an object's middle (a part of its
bounding sphere's radius, which a vehicle's is well above its roof) */
#define MARKER_POINT_HEIGHT 0.1f
#define MARKER_HEAD_HEIGHT 0.1f
#define MARKER_OBJECT_HEIGHT 0.1f
#define MARKER_OBJECT_RADIUS_PART 0.5f

/* ---------- structures */

struct marker_message
{
	byte format;
	/* the player (absolute index) */
	byte player_index;
	byte kind;
	/* (config: the host's flags) */
	byte flags;
	/* the object (datum index), NONE for a point */
	long object_index;
	/* the point (an object's: where it was) */
	real point[3];
};

typedef char marker_message_size_assert[sizeof(struct marker_message) == 20 ? 1 : -1];

struct marker_wire
{
	struct distributed_message_header header;
	struct marker_message marker;
};

struct marker
{
	boolean active;
	byte kind;
	long object_index;
	real_point3d point;
	unsigned long shown_at;
};

struct marker_rate
{
	short tokens;
	unsigned long refilled_at;
	boolean known;
};

/* ---------- globals */

static struct
{
	/* in a game (its pings let go as the next starts), and the game time
	seen last (a new game's starts lower) */
	boolean in_game;
	long last_game_time;
	struct marker markers[MAXIMUM_MARKER_PLAYERS];
	/* the host's: each player's count; a client's: its own players' */
	struct marker_rate rates[MAXIMUM_MARKER_PLAYERS];
	/* a client: the host's settings, as it sent them */
	boolean config_known;
	byte config_flags;
	/* the host: its settings, as it last sent them */
	boolean config_sent_known;
	byte config_sent_flags;
	unsigned long config_sent_at;
	/* the keyboard's ping held (an edge presses) */
	boolean key_held;
	/* the arrow and the sound, as this game's map has them */
	boolean looked;
	short nav_index;
	long sound_index;
	/* a message of another format noted (once) */
	boolean other_format_noted;
	/* (a test's burst) the client's own count not kept */
	boolean unchecked;
	/* the host: the refusals logged this game */
	short refusals_logged;
} markers_globals;

/* ---------- the pure parts */

boolean network_markers_point_valid(
	real x,
	real y,
	real z)
{
	/* (a NaN fails each) */
	return x > -MARKER_WORLD_LIMIT && x < MARKER_WORLD_LIMIT &&
		y > -MARKER_WORLD_LIMIT && y < MARKER_WORLD_LIMIT &&
		z > -MARKER_WORLD_LIMIT && z < MARKER_WORLD_LIMIT;
}

boolean network_markers_same_spot(
	long object_index,
	real const *point,
	long last_object_index,
	real const *last_point)
{
	real dx = point[0] - last_point[0];
	real dy = point[1] - last_point[1];
	real dz = point[2] - last_point[2];

	if (object_index != NONE || last_object_index != NONE)
		return object_index == last_object_index;
	return dx * dx + dy * dy + dz * dz <= MARKER_SAME_SPOT * MARKER_SAME_SPOT;
}

/* ---------- the game */

static boolean markers_network_game(
	void)
{
	short connection = game_connection();

	return connection == _game_connection_network_client || connection == _game_connection_network_server;
}

/* (the host, or a game of this machine alone) its setting */
static byte markers_own_config_flags(
	void)
{
	static byte flags = 0;
	static unsigned long read_at = (unsigned long)-1;

	if (read_at != config_changes())
	{
		read_at = config_changes();
		flags = config_boolean("network.pings") ? (byte)FLAG(_marker_config_on_bit) : 0;
	}
	return flags;
}

/* whether pings are played in this game: by this machine's setting, or its
host's, in a game with allies (the campaign, a game with teams), an OpenH1
one when networked, split screen's on one machine */
static boolean markers_available(
	void)
{
	/* (in_game first: the pads are read before the game's time is made) */
	if (!markers_globals.in_game || !game_in_progress())
		return FALSE;
	if (game_engine_running() && !game_engine_has_teams())
		return FALSE;
	switch (game_connection())
	{
	case _game_connection_network_server:
		return network_standard_agreed_rate() != 0 && TEST_FLAG(markers_own_config_flags(), _marker_config_on_bit);
	case _game_connection_network_client:
		return network_standard_agreed_rate() != 0 && markers_globals.config_known &&
			TEST_FLAG(markers_globals.config_flags, _marker_config_on_bit);
	default:
		/* (a game of this machine alone: with players to ping for, split
		screen's; a player alone keeps the D-pad's strafe) */
		return local_player_count() > 1 && TEST_FLAG(markers_own_config_flags(), _marker_config_on_bit);
	}
}

/* whether the viewer sees the pinger's pings: their own; every player in
the campaign; a team's in a game with teams */
static boolean markers_sees(
	struct player_datum const *pinger,
	struct player_datum const *viewer)
{
	if (!pinger || !viewer)
		return FALSE;
	if (pinger == viewer)
		return TRUE;
	if (!game_engine_running())
		return TRUE;
	return game_engine_has_teams() && pinger->team_index == viewer->team_index;
}

/* whether this machine muted the pinger's machine (the scoreboard's mute,
network_voice.c, which mutes their voice and text chat too), in a network
game */
static boolean markers_muted_here(
	struct player_datum const *pinger)
{
	return pinger && markers_network_game() && network_voice_machine_muted(pinger->network_player_data.machine_index);
}

/* whether the viewer, on this machine, is shown the pinger's pings: those
markers_sees gives them, but none of a player this machine muted (their own
always) */
static boolean markers_shown_to(
	struct player_datum const *pinger,
	struct player_datum const *viewer)
{
	return markers_sees(pinger, viewer) && (pinger == viewer || !markers_muted_here(pinger));
}

/* a player's colour: their team's in a game with teams, else their own
(the profile's colour, as a free-for-all game draws their armor) */
static void markers_player_color(
	long player_index,
	real_rgb_color *color)
{
	struct player_datum *player = player_get(player_index);

	if (game_engine_running() && game_engine_has_teams())
		game_engine_player_get_change_color(color, player_index);
	else
		*color = player_profile_get_rgb_color(player->network_player_data.primary_color_index);
}

/* where a ping of an object is drawn this frame: over it as the frame draws
it between the ticks (render_interpolation.c), over a biped's head, over
another's middle by its size; at, its middle as the last tick left it (where
the ping stays once the object is gone). A biped's head is its head marker's,
which is read from the pose drawn already: moved with the object again, as
the middle is, it went a tick's motion further, up and down a step a tick
as the biped rose or fell */
static void markers_object_drawn_point(
	long object_index,
	real_point3d *at,
	real_point3d *position)
{
	real radius;

	object_get_bounding_sphere(object_index, at, &radius);
	*position = *at;
	render_interpolation_object_point(object_index, position);
	if (object_try_and_get_and_verify_type(object_index, _object_mask_biped))
	{
		real_point3d head_position;

		unit_get_head_position(object_index, &head_position);
		position->z = head_position.z + MARKER_HEAD_HEIGHT;
	}
	else
	{
		position->z += radius * MARKER_OBJECT_RADIUS_PART + MARKER_OBJECT_HEIGHT;
	}
}

/* the pinger's name over their arrow (drawn at drawn, half_height tall
each way), where the arrow is, not the nearest whole unit of the screen (3
to 5 of a display's pixels) short of it (hud_draw_mark_begin), as a
player's name is: in whole units it stepped against the arrow and the
world under it as the view turned or moved */
static void markers_draw_name(
	long font_index,
	wchar_t const *name,
	real_rgb_color const *color,
	real alpha,
	real_point2d const *drawn,
	real half_height)
{
	struct font_header *font = font_definition_get(font_index);
	real_argb_color text_color;
	rectangle2d bounds;
	point2d whole;

	hud_draw_mark_begin(drawn->x, drawn->y - half_height - 2.0f, &whole);
	bounds.x0 = (short)(whole.x - 160);
	bounds.x1 = (short)(whole.x + 160);
	bounds.y1 = whole.y;
	bounds.y0 = (short)(whole.y - (font->ascending_height + font->descending_height));
	text_color.alpha = alpha;
	text_color.red = color->red;
	text_color.green = color->green;
	text_color.blue = color->blue;
	draw_string_set_draw_mode(font_index, NONE, 2 /* centred */, 0, &text_color);
	rasterizer_text_set_scale(0.75f, (real)whole.x, (real)whole.y);
	rasterizer_draw_unicode_string(&bounds, NULL, NULL, 0, name);
	rasterizer_text_set_scale(1.0f, 0.0f, 0.0f);
	hud_draw_mark_end();
}

/* this map's arrow and sound, looked up once a game */
static void markers_look_up(
	void)
{
	static char const *const arrows[] = { "default", "target_blue", NULL };
	static char const *const sounds[] = { "sound\\sfx\\ui\\cursor", "sound\\sfx\\ui\\forward" };
	short index;

	if (markers_globals.looked)
		return;
	markers_globals.looked = TRUE;
	/* (the campaign's arrow; else the multiplayer goals'; else the first) */
	markers_globals.nav_index = NONE;
	for (index = 0; index < (short)NUMBEROF(arrows) && markers_globals.nav_index == NONE; index++)
		markers_globals.nav_index = hud_find_nav_point_quietly(arrows[index]);
	markers_globals.sound_index = NONE;
	for (index = 0; index < (short)NUMBEROF(sounds) && markers_globals.sound_index == NONE; index++)
		markers_globals.sound_index = tag_loaded(SOUND_DEFINITION_TAG, sounds[index]);
	platform_log("pings: the arrow %d (%s), the sound %s", markers_globals.nav_index,
		markers_globals.nav_index == hud_find_nav_point_quietly("default") ? "default" : "another",
		markers_globals.sound_index != NONE ? "found" : "none");
}

/* a new game: the last one's pings, counts and settings let go */
static void markers_new_game(
	void)
{
	csmemset(markers_globals.markers, 0, sizeof(markers_globals.markers));
	csmemset(markers_globals.rates, 0, sizeof(markers_globals.rates));
	markers_globals.config_known = FALSE;
	markers_globals.config_sent_known = FALSE;
	markers_globals.looked = FALSE;
	markers_globals.refusals_logged = 0;
	markers_globals.nav_index = NONE;
	markers_globals.sound_index = NONE;
}

/* one taken from a count; FALSE if none is left */
static boolean markers_rate_take(
	struct marker_rate *rate,
	unsigned long now)
{
	if (!rate->known)
	{
		rate->known = TRUE;
		rate->tokens = MARKER_BURST;
		rate->refilled_at = now;
	}
	while (rate->tokens < MARKER_BURST && now - rate->refilled_at >= MARKER_REFILL_MILLISECONDS)
	{
		rate->tokens++;
		rate->refilled_at += MARKER_REFILL_MILLISECONDS;
	}
	if (rate->tokens >= MARKER_BURST)
		rate->refilled_at = now;
	if (rate->tokens <= 0)
		return FALSE;
	rate->tokens--;
	return TRUE;
}

/* ---------- shown */

/* a ping as the host decided it (or taken away), on this machine */
static void markers_show(
	short player_index,
	byte kind,
	long object_index,
	real_point3d const *point)
{
	struct marker *marker = &markers_globals.markers[player_index];
	struct player_datum *pinger = distributed_player(player_index);
	short local_player_index;
	boolean seen = FALSE;
	boolean muted = FALSE;

	if (kind == _marker_kind_clear)
	{
		if (marker->active)
			platform_log("pings: player %d's ping taken away", player_index);
		marker->active = FALSE;
		return;
	}
	marker->active = TRUE;
	marker->kind = kind;
	marker->object_index = kind == _marker_kind_object ? object_index : NONE;
	marker->point = *point;
	marker->shown_at = system_milliseconds();
	for (local_player_index = local_player_get_next(NONE); local_player_index != NONE;
		local_player_index = local_player_get_next(local_player_index))
	{
		long viewer_index = local_player_get_player_index(local_player_index);

		if (viewer_index == NONE)
			continue;
		if (markers_shown_to(pinger, player_get(viewer_index)))
			seen = TRUE;
		else if (markers_sees(pinger, player_get(viewer_index)))
			muted = TRUE;
	}
	platform_log("pings: player %d pinged %s %d at %.2f %.2f %.2f%s", player_index,
		kind == _marker_kind_object ? "object" : "point", kind == _marker_kind_object ? (int)object_index : -1,
		point->x, point->y, point->z, seen ? "" : muted ? " (muted here)" : " (not seen here)");
	markers_look_up();
	if (seen && markers_globals.sound_index != NONE)
		unspatialized_impulse_sound_new(markers_globals.sound_index, 1.0f);
}

/* ---------- the host */

/* (the host) a ping refused, logged (MARKER_REFUSALS_LOGGED a game at most) */
static void markers_refused(
	short player_index,
	char const *why)
{
	if (markers_globals.refusals_logged >= MARKER_REFUSALS_LOGGED)
		return;
	markers_globals.refusals_logged++;
	platform_log("pings: a ping of player %d refused: %s", player_index, why);
}

/* (the host) a message to every OpenH1 client machine with a player who
sees the pinger's pings (all of them: every OpenH1 client machine) */
static void markers_host_send(
	struct marker_message const *body,
	struct player_datum const *pinger)
{
	long machine_indices[MAXIMUM_MARKER_MACHINES];
	short count;
	short index;
	struct marker_wire message;

	if (game_connection() != _game_connection_network_server)
		return;
	count = distributed_client_machines(machine_indices, MAXIMUM_MARKER_MACHINES);
	for (index = 0; index < count; index++)
	{
		boolean sees = pinger == NULL;

		if (!network_standard_machine_is_openh1(machine_indices[index]))
			continue;
		if (!sees)
		{
			struct data_iterator iterator;
			struct player_datum *player;

			data_iterator_new(&iterator, player_data);
			while (!sees && (player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
			{
				sees = !player->quit_out_of_game &&
					distributed_machine_has_player(machine_indices[index],
						(short)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index)) &&
					markers_sees(pinger, player);
			}
		}
		if (sees)
		{
			csmemset(&message, 0, sizeof(message));
			message.marker = *body;
			distributed_send_to_machine_reliably(machine_indices[index], &message, _distributed_message_marker_down,
				1, (word)sizeof(message));
		}
	}
}

/* (the host, or a game of this machine alone) a player's ping, checked,
decided (a new one, or the last taken away), sent on and shown */
static void markers_host_take(
	short player_index,
	byte kind,
	long object_index,
	real_point3d const *point)
{
	struct player_datum *pinger = distributed_player(player_index);
	struct marker *marker;
	struct marker_message body;
	unsigned long now = system_milliseconds();
	long unit_index;
	real_point3d shown = *point;
	real last[3];
	real here[3];

	if (!pinger || pinger->quit_out_of_game || !markers_available() ||
		(kind != _marker_kind_point && kind != _marker_kind_object) ||
		!network_markers_point_valid(point->x, point->y, point->z))
	{
		markers_refused(player_index, "not a ping of the game");
		return;
	}
	/* (a player the host muted for everyone: network_chat.c) */
	if (markers_network_game() && network_chat_host_mute_minutes(player_index) > 0)
	{
		markers_refused(player_index, "muted by the host");
		return;
	}
	/* (a player with a living unit, near enough the point) */
	unit_index = distributed_living_unit(pinger);
	if (unit_index == NONE || distance3d(&object_get(unit_index)->object.position, point) > MARKER_HOST_RANGE)
	{
		markers_refused(player_index, "no living unit near the point");
		return;
	}
	if (!markers_rate_take(&markers_globals.rates[player_index], now))
	{
		markers_refused(player_index, "too many at once");
		return;
	}
	/* (an object of the kinds that is there; else the point) */
	if (kind == _marker_kind_object && !object_try_and_get_and_verify_type(object_index, MARKER_OBJECT_MASK))
		kind = _marker_kind_point;
	if (kind == _marker_kind_object)
	{
		real radius;

		object_get_bounding_sphere(object_index, &shown, &radius);
	}
	else
	{
		object_index = NONE;
	}
	marker = &markers_globals.markers[player_index];
	last[0] = marker->point.x;
	last[1] = marker->point.y;
	last[2] = marker->point.z;
	here[0] = shown.x;
	here[1] = shown.y;
	here[2] = shown.z;
	csmemset(&body, 0, sizeof(body));
	body.format = MARKER_FORMAT;
	body.player_index = (byte)player_index;
	body.kind = kind;
	body.object_index = object_index;
	body.point[0] = shown.x;
	body.point[1] = shown.y;
	body.point[2] = shown.z;
	/* (its last again, still shown: taken away) */
	if (marker->active && now - marker->shown_at < MARKER_SHOWN_MILLISECONDS + MARKER_FADE_MILLISECONDS &&
		network_markers_same_spot(object_index, here, marker->kind == _marker_kind_object ? marker->object_index : NONE,
			last))
	{
		body.kind = _marker_kind_clear;
		body.object_index = NONE;
	}
	markers_host_send(&body, pinger);
	markers_show(player_index, body.kind, body.object_index, &shown);
}

/* (the host, in a game) its settings sent to every OpenH1 client as they
change, and each MARKER_CONFIG_MILLISECONDS */
static void markers_host_update_config(
	unsigned long now)
{
	byte flags = markers_own_config_flags();
	struct marker_message body;

	if (markers_globals.config_sent_known && flags == markers_globals.config_sent_flags &&
		now - markers_globals.config_sent_at < MARKER_CONFIG_MILLISECONDS)
	{
		return;
	}
	markers_globals.config_sent_known = TRUE;
	markers_globals.config_sent_flags = flags;
	markers_globals.config_sent_at = now;
	csmemset(&body, 0, sizeof(body));
	body.format = MARKER_FORMAT;
	body.player_index = NO_PLAYER;
	body.kind = _marker_kind_config;
	body.flags = flags;
	body.object_index = NONE;
	markers_host_send(&body, NULL);
}

/* ---------- aiming */

/* the object a hit object stands for: a held weapon's unit */
static long markers_object_marked(
	long object_index)
{
	struct object_datum *object = object_try_and_get(object_index);

	if (object && object->object.parent_object_index != NONE &&
		object_try_and_get_and_verify_type(object_index, _object_mask_item))
	{
		object_index = object->object.parent_object_index;
	}
	return object_try_and_get_and_verify_type(object_index, MARKER_OBJECT_MASK) ? object_index : NONE;
}

/* the outermost object an object is in (a rider's vehicle) */
static long markers_root_object(
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

/* what a local player aims at: TRUE with the kind, the object and the point */
static boolean markers_aim(
	short local_player_index,
	byte *kind,
	long *object_index,
	real_point3d *point)
{
	long player_index = local_player_get_player_index(local_player_index);
	struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
	struct observer_result const *camera;
	struct collision_result result;
	real_point3d origin;
	real_vector3d forward;
	real_vector3d vector;
	long unit_index;
	long root_index;
	long hit_object_index = NONE;
	real reach = MARKER_RANGE;
	boolean hit = FALSE;
	short attempt;

	if (!player || (unit_index = distributed_living_unit(player)) == NONE)
		return FALSE;
	camera = observer_get_camera(local_player_index);
	if (!camera)
		return FALSE;
	origin = camera->position;
	forward = camera->forward;
	if (normalize3d(&forward) == 0.0f)
		return FALSE;
	root_index = markers_root_object(unit_index);
	if (global_current_collision_user_depth >= MAXIMUM_COLLISION_USER_STACK_DEPTH)
		return FALSE;
	global_current_collision_users[global_current_collision_user_depth++] = _collision_user_ui;
	/* (past the player's own unit and what it rides, which the line from a
	camera behind them passes through) */
	for (attempt = 0; attempt < 4; attempt++)
	{
		scale_vector3d(&forward, reach, &vector);
		hit = collision_test_vector(MARKER_AIM_FLAGS, &origin, &vector, root_index, &result);
		if (!hit || result.type != _collision_result_object || markers_root_object(result.object_index) != root_index)
			break;
		reach -= reach * result.t + 0.05f;
		origin = result.point;
		origin.x += forward.i * 0.05f;
		origin.y += forward.j * 0.05f;
		origin.z += forward.k * 0.05f;
		hit = FALSE;
		if (reach <= 0.0f)
			break;
	}
	--global_current_collision_user_depth;
	if (hit && result.type == _collision_result_object)
		hit_object_index = markers_object_marked(result.object_index);
	/* (else the nearest object the aim passes near, before what it hit) */
	if (hit_object_index == NONE)
	{
		struct object_iterator iterator;
		struct object_datum *object;
		real hit_distance = hit ? distance3d(&camera->position, &result.point) : MARKER_RANGE;
		real best = 0.0f;

		object_iterator_new(&iterator, MARKER_OBJECT_MASK, 0);
		while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
		{
			real_point3d center;
			real_vector3d to;
			real radius;
			real along;
			real across;
			real reach_across;

			if (iterator.index == unit_index || markers_root_object(iterator.index) == root_index ||
				object->object.parent_object_index != NONE)
			{
				continue;
			}
			object_get_bounding_sphere(iterator.index, &center, &radius);
			vector_from_points3d(&camera->position, &center, &to);
			along = dot_product3d(&to, &forward);
			if (along <= 0.0f || along - radius > hit_distance)
				continue;
			across = square_root(MAX(0.0f, dot_product3d(&to, &to) - along * along));
			reach_across = radius + along * MARKER_PICK_TANGENT;
			if (across > reach_across)
				continue;
			/* (the nearest the line, as its reach measures it) */
			if (hit_object_index == NONE || across / reach_across < best)
			{
				hit_object_index = iterator.index;
				best = across / reach_across;
			}
		}
	}
	if (hit_object_index != NONE)
	{
		real radius;

		*kind = _marker_kind_object;
		*object_index = hit_object_index;
		object_get_bounding_sphere(hit_object_index, point, &radius);
		return TRUE;
	}
	if (!hit)
		return FALSE;
	*kind = _marker_kind_point;
	*object_index = NONE;
	*point = result.point;
	return TRUE;
}

/* a local player's ping: aimed, then the host's to decide; forged: (a
test's client) sent as another machine's player's, as a changed build could,
for the host to refuse */
static boolean markers_ping_as(
	short local_player_index,
	boolean forged)
{
	long player_index = local_player_get_player_index(local_player_index);
	short absolute_index;
	byte kind;
	long object_index;
	real_point3d point;

	if (player_index == NONE || !markers_available() || cinematic_in_progress() ||
		!markers_aim(local_player_index, &kind, &object_index, &point))
	{
		return FALSE;
	}
	absolute_index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(player_index);
	if (forged)
	{
		struct data_iterator iterator;
		struct player_datum *player;

		absolute_index = NONE;
		data_iterator_new(&iterator, player_data);
		while (absolute_index == NONE && (player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (player->local_player_index == NONE)
				absolute_index = (short)DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index);
		}
		if (absolute_index == NONE || game_connection() != _game_connection_network_client)
			return FALSE;
	}
	if (absolute_index < 0 || absolute_index >= MAXIMUM_MARKER_PLAYERS)
		return FALSE;
	if (game_connection() != _game_connection_network_client)
	{
		markers_host_take(absolute_index, kind, object_index, &point);
		return TRUE;
	}
	/* (not more than the host takes; a test's burst all sent, for the host
	to count) */
	if (!forged && !markers_globals.unchecked &&
		!markers_rate_take(&markers_globals.rates[absolute_index], system_milliseconds()))
	{
		return FALSE;
	}
	{
		struct marker_wire message;

		csmemset(&message, 0, sizeof(message));
		message.marker.format = MARKER_FORMAT;
		message.marker.player_index = (byte)absolute_index;
		message.marker.kind = kind;
		message.marker.object_index = object_index;
		message.marker.point[0] = point.x;
		message.marker.point[1] = point.y;
		message.marker.point[2] = point.z;
		distributed_send(&message, _distributed_message_marker_up, 1, (word)sizeof(message),
			_distributed_to_host_reliably);
	}
	return TRUE;
}

static boolean markers_ping(
	short local_player_index)
{
	return markers_ping_as(local_player_index, FALSE);
}

/* ---------- public code */

unsigned char network_markers_pad_open(
	short port)
{
	return markers_available() && controller_remap_local_player(port) != NONE && controller_remap_playing(port) &&
		!cinematic_in_progress();
}

void network_markers_update(
	void)
{
	unsigned long now = system_milliseconds();
	short port;
	boolean key;

	/* (a game: begun, or a new one, whose time starts again) */
	if (!game_in_progress() || (!markers_network_game() && game_connection() != _game_connection_local))
	{
		if (markers_globals.in_game)
			markers_new_game();
		markers_globals.in_game = FALSE;
	}
	else
	{
		if (!markers_globals.in_game || game_time_get() < markers_globals.last_game_time)
			markers_new_game();
		markers_globals.in_game = TRUE;
		markers_globals.last_game_time = game_time_get();
		if (game_connection() == _game_connection_network_server && network_standard_agreed_rate() != 0)
			markers_host_update_config(now);
	}
	/* (the presses, every port's taken whether they ping or not: a custom
	layout's PING, else the stock layouts' D-pad Right; the keyboard's,
	the first player's) */
	for (port = 0; port < 4; port++)
	{
		boolean pressed;
		unsigned char layout[CONTROLLER_REMAP_ACTIONS];

		if (controller_remap_read(port, layout))
			pressed = controller_remap_pressed(port, 1u << PAD_PING) != 0;
		else
			pressed = halo_ping_pad_pressed(port) != 0;
		if (pressed && network_markers_pad_open(port))
			markers_ping(controller_remap_local_player(port));
	}
	key = (halo_keyboard_actions(0) & FLAG(HALO_KEYBOARD_PING)) != 0;
	if (key && !markers_globals.key_held && markers_available() && !console_is_active() && !network_chat_typing())
	{
		short local_player_index = controller_remap_local_player(0);

		if (local_player_index == NONE)
			local_player_index = local_player_get_next(NONE);
		if (local_player_index != NONE && !ui_widgets_active_for_local_player(local_player_index))
			markers_ping(local_player_index);
	}
	markers_globals.key_held = key;
}

void network_markers_render(
	short local_player_index)
{
	unsigned long now = system_milliseconds();
	long viewer_index;
	struct player_datum *viewer;
	real_point3d head;
	long font_index;
	short player_index;

	if (!markers_globals.in_game || local_player_index == NONE || cinematic_in_progress())
		return;
	viewer_index = local_player_get_player_index(local_player_index);
	viewer = viewer_index != NONE ? player_get(viewer_index) : NULL;
	if (!viewer || viewer->unit_index == NONE)
		return;
	markers_look_up();
	if (markers_globals.nav_index == NONE)
		return;
	unit_get_head_position(viewer->unit_index, &head);
	font_index = hud_get_font_index();
	for (player_index = 0; player_index < MAXIMUM_MARKER_PLAYERS; player_index++)
	{
		struct marker *marker = &markers_globals.markers[player_index];
		struct player_datum *pinger;
		unsigned long age;
		real_point3d position;
		real_rgb_color color;
		real_point2d drawn;
		real half_height;
		real alpha;
		short drawn_type;
		long reference_index = NONE;

		if (!marker->active)
			continue;
		age = now - marker->shown_at;
		pinger = distributed_player(player_index);
		if (age >= MARKER_SHOWN_MILLISECONDS + MARKER_FADE_MILLISECONDS || !pinger || pinger->quit_out_of_game)
		{
			marker->active = FALSE;
			continue;
		}
		if (!markers_shown_to(pinger, viewer))
			continue;
		/* (over its object as drawn, while it is there; then where it was) */
		if (marker->kind == _marker_kind_object && object_try_and_get_and_verify_type(marker->object_index,
			MARKER_OBJECT_MASK))
		{
			markers_object_drawn_point(marker->object_index, &marker->point, &position);
			reference_index = marker->object_index;
		}
		else
		{
			if (marker->kind == _marker_kind_object)
			{
				marker->kind = _marker_kind_point;
				marker->object_index = NONE;
			}
			position = marker->point;
			position.z += MARKER_POINT_HEIGHT;
		}
		alpha = age < MARKER_SHOWN_MILLISECONDS ? 1.0f :
			1.0f - (real)(age - MARKER_SHOWN_MILLISECONDS) / (real)MARKER_FADE_MILLISECONDS;
		markers_player_color(DATUM_INDEX_NEW(player_index, pinger->identifier), &color);
		if (!hud_render_nav_point_colored(local_player_index, &position, markers_globals.nav_index,
			hud_get_nav_point_render_type(local_player_index, &head, &position, reference_index), &color, alpha,
			&drawn, &half_height, &drawn_type))
		{
			continue;
		}
		/* (the player's name over the arrow, on the screen) */
		if (font_index != NONE && drawn_type != 1 /* off the screen */)
		{
			wchar_t name[NUMBEROF(pinger->name) + 1];
			short index;

			for (index = 0; index < (short)NUMBEROF(pinger->name); index++)
				name[index] = pinger->name[index];
			name[NUMBEROF(pinger->name)] = 0;
			markers_draw_name(font_index, name, &color, alpha, &drawn, half_height);
		}
	}
}

boolean network_markers_ping_aimed(
	void)
{
	short local_player_index = local_player_get_next(NONE);

	return local_player_index != NONE && markers_ping(local_player_index);
}

boolean network_markers_ping_forged(
	void)
{
	short local_player_index = local_player_get_next(NONE);

	return local_player_index != NONE && markers_ping_as(local_player_index, TRUE);
}

short network_markers_ping_burst(
	short count)
{
	short local_player_index = local_player_get_next(NONE);
	short sent = 0;
	short index;

	markers_globals.unchecked = TRUE;
	for (index = 0; index < count && local_player_index != NONE; index++)
		sent += markers_ping(local_player_index) ? 1 : 0;
	markers_globals.unchecked = FALSE;
	return sent;
}

short network_markers_shown(
	short *player_indices,
	short maximum)
{
	unsigned long now = system_milliseconds();
	short count = 0;
	short player_index;

	for (player_index = 0; player_index < MAXIMUM_MARKER_PLAYERS && count < maximum; player_index++)
	{
		struct marker const *marker = &markers_globals.markers[player_index];

		if (marker->active && now - marker->shown_at < MARKER_SHOWN_MILLISECONDS + MARKER_FADE_MILLISECONDS &&
			!markers_muted_here(distributed_player(player_index)))
		{
			player_indices[count++] = player_index;
		}
	}
	return count;
}

/* ---------- the messages */

boolean network_markers_handles_message(
	word const *message,
	word size)
{
	struct distributed_message_header header;

	if (size < sizeof(header))
		return FALSE;
	csmemcpy(&header, message, sizeof(header));
	return header.type == _distributed_message_marker_up || header.type == _distributed_message_marker_down;
}

void network_markers_handle_message(
	long machine_index,
	word const *message,
	word size,
	boolean from_stream)
{
	struct distributed_message_header header;
	struct marker_message body;
	real_point3d point;

	/* (whole, one, over a stream, in an OpenH1 game under way) */
	if (size != sizeof(header) + sizeof(body) || !from_stream || !game_in_progress() || !markers_globals.in_game ||
		network_standard_agreed_rate() == 0)
	{
		return;
	}
	csmemcpy(&header, message, sizeof(header));
	csmemcpy(&body, (byte const *)message + sizeof(header), sizeof(body));
	if (header.count != 1)
		return;
	if (body.format != MARKER_FORMAT)
	{
		if (!markers_globals.other_format_noted)
			platform_log("pings: a message of another format (%d), not taken", body.format);
		markers_globals.other_format_noted = TRUE;
		return;
	}
	point.x = body.point[0];
	point.y = body.point[1];
	point.z = body.point[2];
	if (header.type == _distributed_message_marker_up)
	{
		/* (the host's, from a client machine's own player) */
		if (game_connection() != _game_connection_network_server || machine_index < 0 ||
			machine_index >= MAXIMUM_MARKER_MACHINES || body.player_index >= MAXIMUM_MARKER_PLAYERS ||
			!distributed_machine_has_player(machine_index, body.player_index) ||
			(body.kind != _marker_kind_point && body.kind != _marker_kind_object))
		{
			markers_refused(body.player_index, "not a player of its machine's");
			return;
		}
		markers_host_take(body.player_index, body.kind, body.object_index, &point);
		return;
	}
	/* (a client's, from its host) */
	if (game_connection() != _game_connection_network_client || machine_index != NONE)
		return;
	if (body.kind == _marker_kind_config)
	{
		markers_globals.config_known = TRUE;
		markers_globals.config_flags = body.flags;
		return;
	}
	if (body.kind >= NUMBER_OF_MARKER_KINDS || body.player_index >= MAXIMUM_MARKER_PLAYERS ||
		distributed_player_from_byte(body.player_index) == NONE ||
		!network_markers_point_valid(point.x, point.y, point.z))
	{
		return;
	}
	/* (an object this machine does not have: its point) */
	if (body.kind == _marker_kind_object && !object_try_and_get_and_verify_type(body.object_index, MARKER_OBJECT_MASK))
		body.kind = _marker_kind_point;
	markers_show(body.player_index, body.kind, body.object_index, &point);
}
