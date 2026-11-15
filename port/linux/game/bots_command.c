/*
BOTS_COMMAND.C

The host directs its bots (bots_command.h; docs/bot_control.md): the
command lines, the orders and traits they give, the places a map's orders
name, and the events the watch reads.

A line is parsed twice: as it comes (on the main thread, between frames),
to answer at once whether it is one the game takes, and again as the tick
that runs the bots begins (bots_command_tick_begin), where an order, a
trait or a team move is applied, every bot of it at once, in the order the
lines came. That tick's demo records the line (demo.c's W record), and
what the order resolved to (a place, a player) is the game state's of that
tick, so a game plays the same however its lines came and whenever the
frames ran. Nothing here draws a random number.

An order is a goal, not the bot's hands: bots_command_objective gives
bots.c a bots_objectives.h objective in place of the game's (to a point,
pressing on through fights; about a point, defending it; with a teammate;
after an enemy as the bot's team last knew of them, never where they are
now unless it saw them), and the bot walks, fights, aims and picks up as
it always does. An order on the enemy's flag goes for the flag itself
(it takes it, and the game's own play runs it home); on a carried flag, it
hunts the enemy carrying it or escorts the teammate who is, as the HUD
shows a player. An attack on a player the bot's team has not seen is a
search: the bot plays its own game, and goes for that player first once
seen (bots_command_target_bonus).
*/

/* ---------- headers */

#include "cseries.h"
#include "math/real_math.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "items/items.h"
#include "items/weapons.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"
#include "networking/network_game_globals.h"
#include "main/console.h"

#include "bots.h"
#include "bots_command.h"
#include "bots_knowledge.h"
#include "bots_objectives.h"
#include "bots_vehicles.h"
#include "demo.h"
#include "server_team_switch.h"

#include "../src/port_config.h" /* port: port/linux/src/port_config.c */
#include "../src/server_json.h" /* port: port/linux/src/server_json.c */

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(char const *format, ...);

/* ---------- constants */

enum
{
	/* the events kept for the watch (bots events), oldest let go */
	BOTS_COMMAND_EVENTS = 512,
	/* a name's size (a place's, a player's) */
	BOTS_COMMAND_NAME_SIZE = 48,
	/* the places of a map named at most */
	BOTS_COMMAND_PLACES = 96,
	/* the words of a line */
	BOTS_COMMAND_WORDS = 24,
	/* a trait's value, a weapon's name */
	BOTS_COMMAND_VALUE_SIZE = 32,
};

/* an order this near its point (across, and up or down) is there */
#define BOTS_COMMAND_ARRIVE_REACH 4.0f
#define BOTS_COMMAND_ARRIVE_HEIGHT 4.0f
/* the ground about a point a defense keeps to, a hold, a follower's
distance, and the ground kept about the point an order went to */
#define BOTS_COMMAND_DEFEND_RADIUS 10.0f
#define BOTS_COMMAND_HOLD_RADIUS 2.5f
#define BOTS_COMMAND_FOLLOW_RADIUS 3.0f
#define BOTS_COMMAND_STAY_RADIUS 6.0f
/* how much lower an attacked player's score as a target is */
#define BOTS_COMMAND_TARGET_BONUS 25.0f

/* what a line asks */
enum
{
	_bots_request_help = 0,
	_bots_request_status,
	_bots_request_bots,
	_bots_request_order,
	_bots_request_set,
	_bots_request_add,
	_bots_request_remove,
	_bots_request_fill,
	_bots_request_team,
	_bots_request_places,
	_bots_request_events,
	NUMBER_OF_BOTS_REQUESTS,
};

/* a trait set */
enum
{
	_bots_trait_skill = 0,
	_bots_trait_aggression,
	_bots_trait_teamwork,
	_bots_trait_weapon,
	_bots_trait_vehicles,
	_bots_trait_style,
	_bots_trait_reset,
	NUMBER_OF_BOTS_TRAITS,
};

/* an order's state */
enum
{
	_bots_order_state_active = 0,
	/* there (an order to a place: it stays about it), or its target dead */
	_bots_order_state_done,
	_bots_order_state_failed,
};

/* what an order's target is */
enum
{
	_bots_target_none = 0,
	/* a point (a place of the map, coordinates) */
	_bots_target_point,
	/* an object (a flag, a vehicle), where it is each tick */
	_bots_target_object,
	/* a player */
	_bots_target_player,
};

/* the vehicles' bits (bots_vehicles.h's kinds) */
#define BOTS_COMMAND_ALL_VEHICLES 0xFFFFFFFFUL

/* ---------- structures */

/* a line, parsed */
struct bots_command_request
{
	short kind;
	boolean json;
	/* the bots it is for: "all", "red", "blue", names and numbers, with commas */
	char who[BOTS_COMMAND_LINE_SIZE];
	/* order: its kind and its target's words */
	short order;
	char target[BOTS_COMMAND_LINE_SIZE];
	/* set: the trait and its value */
	short trait;
	char value[BOTS_COMMAND_LINE_SIZE];
	/* add, remove, fill: how many; add, team: the team (NONE: the game's
	choice); events: after which */
	long count;
	short team;
	long after;
};

struct bots_command_target
{
	short kind;
	real_point3d point;
	long object_index;
	long player_index;
	char name[BOTS_COMMAND_NAME_SIZE];
};

struct bots_command_order
{
	short kind;
	short state;
	struct bots_command_target target;
	/* the words it was given ("blue base"), and when */
	char words[BOTS_COMMAND_NAME_SIZE];
	long given_tick;
};

struct bots_command_traits
{
	short skill;
	real aggression;
	real teamwork;
	char weapon[BOTS_COMMAND_VALUE_SIZE];
	unsigned long vehicles;
	short style;
};

/* a named place of the map */
struct bots_command_place
{
	char name[BOTS_COMMAND_NAME_SIZE];
	char kind[16];
	short team;
	real_point3d point;
	long object_index;
};

/* ---------- globals */

static char const *const bots_command_order_names[NUMBER_OF_BOTS_COMMAND_ORDERS] =
{
	"free", "attack", "defend", "hold", "follow", "goto", "retreat", "regroup", "push",
};
static char const *const bots_command_order_state_names[] = { "active", "done", "failed" };
static char const *const bots_command_skill_names[] = { "easy", "normal", "heroic", "legendary" };
static char const *const bots_command_trait_names[NUMBER_OF_BOTS_TRAITS] =
{
	"skill", "aggression", "teamwork", "weapon", "vehicles", "style", "reset",
};
static char const *const bots_command_team_names[] = { "red", "blue" };
/* the vehicles by kind (bots_vehicles.h) */
static char const *const bots_command_vehicle_names[NUMBER_OF_BOTS_VEHICLE_KINDS] =
{
	"", "warthog", "ghost", "scorpion", "banshee", "other",
};
/* the styles: their aggression and teamwork */
static struct
{
	char const *name;
	real aggression;
	real teamwork;
} const bots_command_styles[] =
{
	{ "default", 0.5f, 1.0f },
	{ "rusher", 0.9f, 0.3f },
	{ "defender", 0.2f, 1.0f },
	{ "lone", 0.6f, 0.0f },
};
#define NUMBER_OF_BOTS_COMMAND_STYLES ((short)(sizeof(bots_command_styles) / sizeof(bots_command_styles[0])))
static char const *const bots_command_goal_names[] =
{
	"looking about", "after an enemy", "roaming", "fetching a weapon", "boarding a vehicle", "taking cover",
	"timing a power item", "with its team", "playing the objective",
};

static struct
{
	boolean traits_ready;
	struct bots_command_order orders[BOTS_MAXIMUM];
	struct bots_command_traits traits[BOTS_MAXIMUM];
	/* the team each bot joins as (add <n> <team>), NONE: the game's */
	short join_teams[BOTS_MAXIMUM];

	/* the lines waiting for the next tick */
	char queue[BOTS_COMMAND_QUEUE][BOTS_COMMAND_LINE_SIZE];
	long queue_first;
	long queue_count;

	/* the watch's events: the next one's number, and those kept */
	long next_event;
	char *events[BOTS_COMMAND_EVENTS];
	long event_numbers[BOTS_COMMAND_EVENTS];

	/* the flags as the last tick saw them, for their events */
	boolean flags_known;
	long flag_carriers[2];
	boolean flag_home[2];
} bots_command;

/* ---------- text */

/* the text in lower case, its _ and - as spaces, runs of spaces as one,
none at the ends */
static void bots_command_normalize(char const *text, char *out, long size)
{
	long length = 0;
	boolean space = FALSE;

	for (; *text && length < size - 1; text++)
	{
		char character = (char)tolower((unsigned char)*text);

		if (character == '_' || character == '-' || character == ' ' || character == '\t' || character == '"')
		{
			space = length > 0;
			continue;
		}
		if (space && length < size - 2)
			out[length++] = ' ';
		space = FALSE;
		out[length++] = character;
	}
	out[length] = 0;
}

/* the line's words (quotes keep spaces in one), how many */
static long bots_command_words(char *line, char **words, long maximum)
{
	long count = 0;
	char *cursor = line;

	while (*cursor && count < maximum)
	{
		char *end;

		while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
			cursor++;
		if (!*cursor)
			break;
		if (*cursor == '"')
		{
			cursor++;
			end = strchr(cursor, '"');
			words[count++] = cursor;
			if (!end)
				break;
			*end = 0;
			cursor = end + 1;
			continue;
		}
		words[count++] = cursor;
		while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '\r' && *cursor != '\n')
			cursor++;
		if (*cursor)
			*cursor++ = 0;
	}
	return count;
}

static short bots_command_name_index(char const *word, char const *const *names, short count)
{
	short index;

	for (index = 0; index < count; index++)
	{
		if (names[index][0] && !csstrcasecmp(word, names[index]))
			return index;
	}
	return NONE;
}

/* "red", "blue" (and "0", "1"); NONE for neither */
static short bots_command_team_word(char const *word)
{
	if (!csstrcasecmp(word, "red") || !strcmp(word, "0"))
		return 0;
	if (!csstrcasecmp(word, "blue") || !strcmp(word, "1"))
		return 1;
	return NONE;
}

/* words from first on, joined by spaces */
static void bots_command_join(char **words, long first, long count, char *out, long size)
{
	long index, length = 0;

	out[0] = 0;
	for (index = first; index < count && length < size - 1; index++)
		length += snprintf(out + length, (size_t)(size - length), "%s%s", index > first ? " " : "", words[index]);
}

/* three numbers, "x,y,z" or "x y z" */
static boolean bots_command_coordinates(char const *text, real_point3d *point)
{
	char copy[BOTS_COMMAND_LINE_SIZE];
	char *cursor = copy, *end;
	double values[3];
	long index;

	snprintf(copy, sizeof(copy), "%s", text);
	for (index = 0; index < 3; index++)
	{
		while (*cursor == ' ' || *cursor == ',')
			cursor++;
		values[index] = strtod(cursor, &end);
		if (end == cursor || !(values[index] > -100000.0 && values[index] < 100000.0))
			return FALSE;
		cursor = end;
	}
	while (*cursor == ' ')
		cursor++;
	if (*cursor)
		return FALSE;
	point->x = (real)values[0];
	point->y = (real)values[1];
	point->z = (real)values[2];
	return TRUE;
}

/* a line's request; FALSE (why, in error) for one the game does not take */
static boolean bots_command_parse(char const *line, struct bots_command_request *request, char *error, long error_size)
{
	char copy[BOTS_COMMAND_LINE_SIZE];
	char *words[BOTS_COMMAND_WORDS];
	long count, index, kept = 0;
	short kind;
	static char const *const verbs[NUMBER_OF_BOTS_REQUESTS] =
	{
		"help", "status", "bots", "order", "set", "add", "remove", "fill", "team", "places", "events",
	};

	csmemset(request, 0, sizeof(*request));
	request->team = NONE;
	request->count = 1;
	error[0] = 0;
	if (strlen(line) >= sizeof(copy))
	{
		snprintf(error, (size_t)error_size, "the line is longer than %d characters", BOTS_COMMAND_LINE_SIZE - 1);
		return FALSE;
	}
	snprintf(copy, sizeof(copy), "%s", line);
	count = bots_command_words(copy, words, BOTS_COMMAND_WORDS);
	/* (--json anywhere) */
	for (index = 0; index < count; index++)
	{
		if (!strcmp(words[index], "--json"))
			request->json = TRUE;
		else
			words[kept++] = words[index];
	}
	count = kept;
	if (!count)
		return TRUE;
	kind = bots_command_name_index(words[0], verbs, NUMBER_OF_BOTS_REQUESTS);
	if (kind == NONE && !csstrcasecmp(words[0], "list"))
		kind = _bots_request_bots;
	if (kind == NONE)
	{
		snprintf(error, (size_t)error_size, "\"%s\" is not a command (bots help lists them)", words[0]);
		return FALSE;
	}
	request->kind = kind;
	switch (kind)
	{
	case _bots_request_order:
	case _bots_request_set:
	case _bots_request_team:
	{
		long next = 2;

		if (count < 3)
		{
			snprintf(error, (size_t)error_size, kind == _bots_request_order ? "order <who> <order> [target]" :
				kind == _bots_request_set ? "set <who> <trait> <value>" : "team <who> <red|blue>");
			return FALSE;
		}
		snprintf(request->who, sizeof(request->who), "%s", words[1]);
		/* ("all red": red's) */
		if (!csstrcasecmp(words[1], "all") && count > 3 && bots_command_team_word(words[2]) != NONE)
		{
			snprintf(request->who, sizeof(request->who), "%s", words[2]);
			next = 3;
		}
		if (kind == _bots_request_order)
		{
			request->order = bots_command_name_index(words[next], bots_command_order_names, NUMBER_OF_BOTS_COMMAND_ORDERS);
			if (request->order == NONE)
			{
				if (!csstrcasecmp(words[next], "guard"))
					request->order = _bots_command_defend;
				else if (!csstrcasecmp(words[next], "clear") || !csstrcasecmp(words[next], "normal"))
					request->order = _bots_command_free;
				else if (!csstrcasecmp(words[next], "go") || !csstrcasecmp(words[next], "move"))
					request->order = _bots_command_goto;
				else
				{
					snprintf(error, (size_t)error_size, "\"%s\" is not an order: attack, defend, hold, follow, push, "
						"retreat, regroup, goto, clear", words[next]);
					return FALSE;
				}
			}
			bots_command_join(words, next + 1, count, request->target, sizeof(request->target));
			/* (go to, attack the: the words alone) */
			if (!strncmp(request->target, "to ", 3))
				memmove(request->target, request->target + 3, strlen(request->target + 3) + 1);
			if (!strncmp(request->target, "the ", 4))
				memmove(request->target, request->target + 4, strlen(request->target + 4) + 1);
			if ((request->order == _bots_command_follow || request->order == _bots_command_goto) && !request->target[0])
			{
				snprintf(error, (size_t)error_size, "%s needs a target: a place (bots places), a player or x,y,z",
					bots_command_order_names[request->order]);
				return FALSE;
			}
			return TRUE;
		}
		if (kind == _bots_request_team)
		{
			request->team = bots_command_team_word(words[next]);
			if (request->team == NONE || count > next + 1)
			{
				snprintf(error, (size_t)error_size, "team <who> <red|blue>");
				return FALSE;
			}
			return TRUE;
		}
		request->trait = bots_command_name_index(words[next], bots_command_trait_names, NUMBER_OF_BOTS_TRAITS);
		if (request->trait == NONE && !csstrcasecmp(words[next], "accuracy"))
			request->trait = _bots_trait_skill;
		if (request->trait == NONE && (!csstrcasecmp(words[next], "vehicle") || !csstrcasecmp(words[next], "profile")))
			request->trait = !csstrcasecmp(words[next], "vehicle") ? _bots_trait_vehicles : _bots_trait_style;
		if (request->trait == NONE)
		{
			snprintf(error, (size_t)error_size, "\"%s\" is not a trait: skill, aggression, teamwork, weapon, vehicles, "
				"style, reset", words[next]);
			return FALSE;
		}
		bots_command_join(words, next + 1, count, request->value, sizeof(request->value));
		if (request->trait == _bots_trait_reset)
			return TRUE;
		if (!request->value[0])
		{
			snprintf(error, (size_t)error_size, "set <who> %s <value>", bots_command_trait_names[request->trait]);
			return FALSE;
		}
		switch (request->trait)
		{
		case _bots_trait_skill:
			if (bots_command_name_index(request->value, bots_command_skill_names, 4) == NONE &&
				csstrcasecmp(request->value, "default"))
			{
				snprintf(error, (size_t)error_size, "skill is easy, normal, heroic, legendary or default");
				return FALSE;
			}
			break;
		case _bots_trait_aggression:
		case _bots_trait_teamwork:
		{
			char *end;
			double value = strtod(request->value, &end);

			if (end == request->value || *end || !(value >= 0.0 && value <= 1.0))
			{
				snprintf(error, (size_t)error_size, "%s is a number from 0 to 1", bots_command_trait_names[request->trait]);
				return FALSE;
			}
			break;
		}
		case _bots_trait_vehicles:
		{
			char value[BOTS_COMMAND_LINE_SIZE];
			char *part;

			snprintf(value, sizeof(value), "%s", request->value);
			if (!csstrcasecmp(value, "all") || !csstrcasecmp(value, "any") || !csstrcasecmp(value, "none"))
				break;
			for (part = strtok(value, ", "); part; part = strtok(NULL, ", "))
			{
				if (bots_command_name_index(part, bots_command_vehicle_names, NUMBER_OF_BOTS_VEHICLE_KINDS - 1) == NONE)
				{
					snprintf(error, (size_t)error_size, "vehicles is all, none, or of warthog, ghost, scorpion, banshee "
						"(with commas)");
					return FALSE;
				}
			}
			break;
		}
		case _bots_trait_style:
		{
			short style;

			for (style = 0; style < NUMBER_OF_BOTS_COMMAND_STYLES; style++)
			{
				if (!csstrcasecmp(request->value, bots_command_styles[style].name))
					break;
			}
			if (style >= NUMBER_OF_BOTS_COMMAND_STYLES)
			{
				snprintf(error, (size_t)error_size, "style is default, rusher, defender or lone");
				return FALSE;
			}
			break;
		}
		default:
			break;
		}
		return TRUE;
	}
	case _bots_request_add:
	case _bots_request_remove:
	case _bots_request_fill:
	{
		long next = 1;

		if (count > next && isdigit((unsigned char)words[next][0]))
		{
			char *end;

			request->count = strtol(words[next], &end, 10);
			if (*end || request->count < (kind == _bots_request_fill ? 2 : 1) || request->count > BOTS_MAXIMUM)
			{
				snprintf(error, (size_t)error_size, "%s takes a number from %d to %d", verbs[kind],
					kind == _bots_request_fill ? 2 : 1, BOTS_MAXIMUM);
				return FALSE;
			}
			next++;
		}
		else if (kind == _bots_request_fill)
		{
			snprintf(error, (size_t)error_size, "fill <players and bots>");
			return FALSE;
		}
		if (kind == _bots_request_add && count > next)
		{
			request->team = bots_command_team_word(words[next]);
			if (request->team == NONE)
			{
				snprintf(error, (size_t)error_size, "add [count] [red|blue]");
				return FALSE;
			}
			next++;
		}
		if (count > next)
		{
			snprintf(error, (size_t)error_size, "%s: \"%s\" is more than it takes", verbs[kind], words[next]);
			return FALSE;
		}
		return TRUE;
	}
	case _bots_request_events:
		request->after = 0;
		if (count > 1)
		{
			char *end;

			request->after = strtol(words[1], &end, 10);
			if (*end || request->after < 0)
			{
				snprintf(error, (size_t)error_size, "events [after]");
				return FALSE;
			}
		}
		return TRUE;
	default:
		return TRUE;
	}
}

/* ---------- reading the game */

/* the game being played that the bots play in, on this machine's host */
static boolean bots_command_game(void)
{
	return game_in_progress() && game_engine_running() && global_network_game_server_get() &&
		game_connection() == _game_connection_network_server;
}

static void bots_command_player_name(struct player_datum const *player, char *name, long size)
{
	long index;

	for (index = 0; index < (long)NUMBEROF(player->name) && index < size - 1 && player->name[index]; index++)
		name[index] = player_name_character_ascii(player->name[index]);
	name[index] = 0;
}

/* where a unit is in the world: in a vehicle, the vehicle's place (its own
is its seat's, in the vehicle) */
static boolean bots_command_unit_position(long unit_index, real_point3d *position)
{
	struct object_datum *object = unit_index != NONE ? object_try_and_get(unit_index) : NULL;

	if (!object)
		return FALSE;
	if (object->object.parent_object_index != NONE && object_try_and_get(object->object.parent_object_index))
		object = object_try_and_get(object->object.parent_object_index);
	*position = object->object.position;
	return TRUE;
}

static struct player_datum *bots_command_live_player(long player_index, real_point3d *position)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	real_point3d where;

	if (!player || player->quit_out_of_game || player->unit_index == NONE ||
		!bots_command_unit_position(player->unit_index, &where))
	{
		return NULL;
	}
	if (position)
		*position = where;
	return player;
}

/* a tag's name's last part ("rocket launcher"), NULL for none */
static char const *bots_command_tag_part(long definition_index)
{
	char const *name = definition_index != NONE ? tag_get_name(definition_index) : NULL;
	char const *part;

	if (!name)
		return NULL;
	part = strrchr(name, '\\');
	return part ? part + 1 : name;
}

static boolean bots_command_flag_state(short team, long *object_index, real_point3d *position, long *carrier,
	boolean *at_home)
{
	return bots_objectives_flag(team, object_index, position, carrier, at_home);
}

/* the point at the middle of the team's players' units (a free-for-all's:
everyone's); FALSE for none */
static boolean bots_command_team_middle(short team, real_point3d *middle)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long count = 0;

	middle->x = middle->y = middle->z = 0.0f;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		real_point3d position;

		if ((team != NONE && player->team_index != team) || !bots_command_live_player(iterator.datum_index, &position))
			continue;
		middle->x += position.x;
		middle->y += position.y;
		middle->z += position.z;
		count++;
	}
	if (!count)
		return FALSE;
	middle->x /= (real)count;
	middle->y /= (real)count;
	middle->z /= (real)count;
	return TRUE;
}

/* the map's places: each team's base (its flag's stand, else the middle of
its starting places) and flag (where it is now in Capture the Flag, else
its stand), the middle between the bases, the hills, the vehicles and the
power items; how many */
static long bots_command_places(struct bots_command_place *places, long maximum)
{
	struct scenario *scenario = global_scenario_get();
	long count = 0, index;
	short team;
	boolean bases[2] = { FALSE, FALSE };
	real_point3d base_points[2];

	if (!scenario)
		return 0;
	for (team = 0; team < 2 && count < maximum; team++)
	{
		long flag_index = find_netgame_flag(NULL, 0.0f, 0.0f, _netgame_flag_ctf_flag, team);
		real_point3d sum = { 0.0f, 0.0f, 0.0f };
		long starts = 0;

		if (flag_index != NONE)
		{
			base_points[team] = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, flag_index,
				struct scenario_netgame_flag)->position;
			bases[team] = TRUE;
		}
		for (index = 0; !bases[team] && index < scenario->players.count; index++)
		{
			struct encounter_player_starting_location *start = TAG_BLOCK_GET_ELEMENT(&scenario->players, index,
				struct encounter_player_starting_location);

			if (start->team_index != team)
				continue;
			sum.x += start->position.x;
			sum.y += start->position.y;
			sum.z += start->position.z;
			starts++;
		}
		if (!bases[team] && starts)
		{
			base_points[team].x = sum.x / (real)starts;
			base_points[team].y = sum.y / (real)starts;
			base_points[team].z = sum.z / (real)starts;
			bases[team] = TRUE;
		}
		if (!bases[team])
			continue;
		csmemset(&places[count], 0, sizeof(places[count]));
		snprintf(places[count].name, sizeof(places[count].name), "%s base", bots_command_team_names[team]);
		snprintf(places[count].kind, sizeof(places[count].kind), "base");
		places[count].team = team;
		places[count].point = base_points[team];
		places[count].object_index = NONE;
		count++;
	}
	for (team = 0; team < 2 && count < maximum; team++)
	{
		long object_index = NONE, carrier = NONE;
		real_point3d position;
		boolean at_home = FALSE;
		long flag_index;

		csmemset(&places[count], 0, sizeof(places[count]));
		places[count].object_index = NONE;
		if (bots_command_flag_state(team, &object_index, &position, &carrier, &at_home))
		{
			places[count].point = position;
			places[count].object_index = object_index;
		}
		else if ((flag_index = find_netgame_flag(NULL, 0.0f, 0.0f, _netgame_flag_ctf_flag, team)) != NONE)
		{
			places[count].point = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, flag_index,
				struct scenario_netgame_flag)->position;
		}
		else
			continue;
		snprintf(places[count].name, sizeof(places[count].name), "%s flag", bots_command_team_names[team]);
		snprintf(places[count].kind, sizeof(places[count].kind), "flag");
		places[count].team = team;
		count++;
	}
	if (bases[0] && bases[1] && count < maximum)
	{
		csmemset(&places[count], 0, sizeof(places[count]));
		snprintf(places[count].name, sizeof(places[count].name), "middle");
		snprintf(places[count].kind, sizeof(places[count].kind), "middle");
		places[count].team = NONE;
		places[count].point.x = 0.5f * (base_points[0].x + base_points[1].x);
		places[count].point.y = 0.5f * (base_points[0].y + base_points[1].y);
		places[count].point.z = 0.5f * (base_points[0].z + base_points[1].z);
		places[count].object_index = NONE;
		count++;
	}
	/* (the hills: each one's first point, numbered by the map's ids) */
	{
		long hills = 0;

		for (index = 0; index < scenario->netgame_flags.count && count < maximum; index++)
		{
			struct scenario_netgame_flag *flag = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index,
				struct scenario_netgame_flag);
			long other;
			boolean seen = FALSE;

			if (flag->type != _netgame_flag_hill)
				continue;
			for (other = 0; other < index; other++)
			{
				struct scenario_netgame_flag *before = TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, other,
					struct scenario_netgame_flag);

				seen |= before->type == _netgame_flag_hill && before->team_index == flag->team_index;
			}
			if (seen)
				continue;
			csmemset(&places[count], 0, sizeof(places[count]));
			snprintf(places[count].name, sizeof(places[count].name), "hill %ld", ++hills);
			snprintf(places[count].kind, sizeof(places[count].kind), "hill");
			places[count].team = NONE;
			places[count].point = flag->position;
			places[count].object_index = NONE;
			count++;
		}
	}
	/* (the vehicles, numbered by kind in the objects' order) */
	{
		struct object_iterator iterator;
		struct object_datum *vehicle;
		long numbers[NUMBER_OF_BOTS_VEHICLE_KINDS];

		csmemset(numbers, 0, sizeof(numbers));
		object_iterator_new(&iterator, _object_mask_vehicle, 0);
		while ((vehicle = (struct object_datum *)object_iterator_next(&iterator)) != NULL && count < maximum)
		{
			short kind = bots_vehicle_kind(iterator.index);
			char const *part = bots_command_tag_part(vehicle->definition_index);

			if (kind == _bots_vehicle_none || TEST_FLAG(vehicle->object.damage_flags, _object_dead_bit))
				continue;
			csmemset(&places[count], 0, sizeof(places[count]));
			if (kind != _bots_vehicle_other)
				snprintf(places[count].name, sizeof(places[count].name), "%s %ld", bots_command_vehicle_names[kind], ++numbers[kind]);
			else
				snprintf(places[count].name, sizeof(places[count].name), "%s %ld", part ? part : "vehicle", ++numbers[kind]);
			snprintf(places[count].kind, sizeof(places[count].kind), "vehicle");
			places[count].team = NONE;
			places[count].point = vehicle->object.position;
			places[count].object_index = iterator.index;
			count++;
		}
	}
	/* (the power items' spawns, as every player can time them) */
	{
		long items = bots_knowledge_item_count();

		for (index = 0; index < items && count < maximum; index++)
		{
			struct bots_item item;
			long number = 1, other;

			if (!bots_knowledge_item(index, &item, NULL) || !item.power)
				continue;
			for (other = 0; other < count; other++)
			{
				if (!strcmp(places[other].kind, "item") && !strncmp(places[other].name, item.name, strlen(item.name)))
					number++;
			}
			csmemset(&places[count], 0, sizeof(places[count]));
			snprintf(places[count].name, sizeof(places[count].name), "%s %ld", item.name, number);
			snprintf(places[count].kind, sizeof(places[count].kind), "item");
			places[count].team = NONE;
			places[count].point = item.position;
			places[count].object_index = NONE;
			count++;
		}
	}
	for (index = 0; index < count; index++)
	{
		char normal[BOTS_COMMAND_NAME_SIZE];

		bots_command_normalize(places[index].name, normal, sizeof(normal));
		snprintf(places[index].name, sizeof(places[index].name), "%s", normal);
	}
	return count;
}

/* the player named (its whole name, else the one whose name starts so),
NONE for none or more than one */
static long bots_command_player_named(char const *text)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long found = NONE, starts = NONE, starts_count = 0;
	char wanted[BOTS_COMMAND_NAME_SIZE];

	bots_command_normalize(text, wanted, sizeof(wanted));
	if (!wanted[0] || !player_data)
		return NONE;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		char name[16], normal[16];

		if (player->quit_out_of_game)
			continue;
		bots_command_player_name(player, name, sizeof(name));
		bots_command_normalize(name, normal, sizeof(normal));
		if (!strcmp(normal, wanted))
			found = iterator.datum_index;
		else if (!strncmp(normal, wanted, strlen(wanted)))
		{
			starts = iterator.datum_index;
			starts_count++;
		}
	}
	return found != NONE ? found : starts_count == 1 ? starts : NONE;
}

/* the first player of this machine (the host's own) */
static long bots_command_host_player(void)
{
	struct data_iterator iterator;
	struct player_datum *player;

	if (!player_data)
		return NONE;
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		if (player->local_player_index == 0 && !player->quit_out_of_game)
			return iterator.datum_index;
	}
	return NONE;
}

/* an order's target for a bot of that team: coordinates; a place ("red
base"; "our flag", "enemy base" by the bot's team; "flag" and "base" alone
its own for a defense, the enemy's otherwise); "me" (the host's player); a
player. FALSE, why in error, for none */
static boolean bots_command_resolve(short order, short team, char const *text, struct bots_command_target *target,
	char *error, long error_size)
{
	struct bots_command_place places[BOTS_COMMAND_PLACES];
	char wanted[BOTS_COMMAND_LINE_SIZE];
	char const *rest;
	long count, index;
	boolean own = order == _bots_command_defend || order == _bots_command_hold || order == _bots_command_retreat;

	csmemset(target, 0, sizeof(*target));
	target->object_index = NONE;
	target->player_index = NONE;
	bots_command_normalize(text, wanted, sizeof(wanted));
	if (!wanted[0])
		return TRUE;
	if (bots_command_coordinates(text, &target->point))
	{
		target->kind = _bots_target_point;
		snprintf(target->name, sizeof(target->name), "%.1f,%.1f,%.1f", target->point.x, target->point.y, target->point.z);
		return TRUE;
	}
	if (!strcmp(wanted, "me") || !strcmp(wanted, "host"))
	{
		target->player_index = bots_command_host_player();
		if (target->player_index == NONE)
		{
			snprintf(error, (size_t)error_size, "this machine has no player in the game");
			return FALSE;
		}
		target->kind = _bots_target_player;
		bots_command_player_name(player_get(target->player_index), target->name, sizeof(target->name));
		return TRUE;
	}
	/* (by the bot's team: ours, theirs) */
	rest = wanted;
	if (!strncmp(wanted, "our ", 4) || !strncmp(wanted, "own ", 4) || !strncmp(wanted, "my ", 3) ||
		!strncmp(wanted, "home ", 5))
	{
		rest = strchr(wanted, ' ') + 1;
		own = TRUE;
	}
	else if (!strncmp(wanted, "enemy ", 6) || !strncmp(wanted, "their ", 6))
	{
		rest = strchr(wanted, ' ') + 1;
		own = FALSE;
	}
	else if (strcmp(wanted, "flag") && strcmp(wanted, "base"))
		rest = NULL;
	if (rest && (!strcmp(rest, "flag") || !strcmp(rest, "base")))
	{
		short wanted_team = team == NONE ? NONE : own ? team : (short)(1 - team);

		if (wanted_team == NONE)
		{
			snprintf(error, (size_t)error_size, "\"%s\" needs a team game", text);
			return FALSE;
		}
		snprintf(wanted, sizeof(wanted), "%s %s", bots_command_team_names[wanted_team], rest);
	}
	count = bots_command_places(places, BOTS_COMMAND_PLACES);
	for (index = 0; index < count; index++)
	{
		/* ("warthog" for "warthog 1", "rocket launcher" for its first) */
		long length = (long)strlen(wanted);

		if (strcmp(places[index].name, wanted) &&
			!(!strncmp(places[index].name, wanted, (size_t)length) && !strcmp(places[index].name + length, " 1")))
		{
			continue;
		}
		snprintf(target->name, sizeof(target->name), "%s", places[index].name);
		target->point = places[index].point;
		target->object_index = places[index].object_index;
		target->kind = target->object_index != NONE ? _bots_target_object : _bots_target_point;
		return TRUE;
	}
	target->player_index = bots_command_player_named(text);
	if (target->player_index != NONE)
	{
		target->kind = _bots_target_player;
		bots_command_player_name(player_get(target->player_index), target->name, sizeof(target->name));
		return TRUE;
	}
	snprintf(error, (size_t)error_size, "\"%s\" is no place (bots places lists them), player or x,y,z", text);
	return FALSE;
}

/* ---------- the events */

static void bots_command_event_add(struct server_json *json)
{
	long slot = bots_command.next_event % BOTS_COMMAND_EVENTS;
	char const *text = server_json_text(json);

	/* (the game's free takes no NULL) */
	if (bots_command.events[slot])
		free(bots_command.events[slot]);
	bots_command.events[slot] = malloc(strlen(text) + 1);
	if (bots_command.events[slot])
		strcpy(bots_command.events[slot], text);
	bots_command.event_numbers[slot] = bots_command.next_event++;
	server_json_free(json);
}

/* an event's start: its number, tick and name (the caller adds its fields
and bots_command_event_end ends it) */
static void bots_command_event_begin(struct server_json *json, char const *name)
{
	server_json_begin(json);
	server_json_object_begin(json, NULL);
	server_json_integer(json, "seq", bots_command.next_event);
	server_json_integer(json, "tick", game_in_progress() ? game_time_get() : 0);
	server_json_string(json, "event", name);
}

static void bots_command_event_end(struct server_json *json)
{
	server_json_object_end(json);
	bots_command_event_add(json);
}

static void bots_command_json_player(struct server_json *json, char const *key, long player_index)
{
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	char name[16];

	if (!player)
	{
		server_json_null(json, key);
		return;
	}
	bots_command_player_name(player, name, sizeof(name));
	server_json_string(json, key, name);
}

static void bots_command_json_point(struct server_json *json, char const *key, real_point3d const *point)
{
	server_json_array_begin(json, key);
	server_json_real(json, NULL, floor(point->x * 100.0 + 0.5) / 100.0);
	server_json_real(json, NULL, floor(point->y * 100.0 + 0.5) / 100.0);
	server_json_real(json, NULL, floor(point->z * 100.0 + 0.5) / 100.0);
	server_json_array_end(json);
}

static void bots_command_order_event(long bot_index, char const *name, char const *reason)
{
	struct server_json json;
	struct bots_command_order const *order = &bots_command.orders[bot_index];

	bots_command_event_begin(&json, name);
	bots_command_json_player(&json, "bot", bots_player_of_bot(bot_index));
	server_json_integer(&json, "number", bot_index + 1);
	server_json_string(&json, "order", bots_command_order_names[order->kind]);
	server_json_string(&json, "target", order->target.name);
	if (reason)
		server_json_string(&json, "reason", reason);
	bots_command_event_end(&json);
}

void bots_command_note_kill(long killing_player_index, long killing_object_index, long dead_player_index)
{
	struct object_datum *killing = killing_object_index != NONE ? object_try_and_get(killing_object_index) : NULL;
	char const *weapon = NULL;
	struct server_json json;
	struct player_datum *dead = dead_player_index != NONE ? player_try_and_get(dead_player_index) : NULL;
	struct player_datum *killer = killing_player_index != NONE ? player_try_and_get(killing_player_index) : NULL;
	long index;

	if (!dead)
		return;
	/* (what killed: a unit's weapon in hand, else the object) */
	if (killing && TEST_FLAG(_object_mask_unit, killing->object.type))
	{
		struct unit_datum *unit = unit_get(killing_object_index);
		long weapon_index = unit->unit.current_weapon_index != NONE ?
			unit_inventory_get_weapon(killing_object_index, unit->unit.current_weapon_index) : NONE;
		struct object_datum *held = weapon_index != NONE ? object_try_and_get(weapon_index) : NULL;

		weapon = held ? bots_command_tag_part(held->definition_index) : NULL;
	}
	else if (killing)
		weapon = bots_command_tag_part(killing->definition_index);
	bots_command_event_begin(&json, "kill");
	bots_command_json_player(&json, "killer", killing_player_index);
	if (killer && VALID_INDEX(killer->team_index, 2))
		server_json_string(&json, "killer_team", bots_command_team_names[killer->team_index]);
	bots_command_json_player(&json, "victim", dead_player_index);
	if (VALID_INDEX(dead->team_index, 2))
		server_json_string(&json, "victim_team", bots_command_team_names[dead->team_index]);
	server_json_boolean(&json, "victim_bot", bots_player_is_bot(dead_player_index));
	if (weapon)
		server_json_string(&json, "weapon", weapon);
	if (bots_command_live_player(dead_player_index, NULL))
	{
		real_point3d position;

		bots_command_live_player(dead_player_index, &position);
		bots_command_json_point(&json, "position", &position);
	}
	bots_command_event_end(&json);
	/* (an attack on that player: done) */
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_command_order *order = &bots_command.orders[index];

		if (order->kind == _bots_command_attack && order->state == _bots_order_state_active &&
			order->target.kind == _bots_target_player && order->target.player_index == dead_player_index)
		{
			order->state = _bots_order_state_done;
			bots_command_order_event(index, "order_done", "killed");
			order->kind = _bots_command_free;
		}
	}
}

/* the flags' events: taken, dropped, returned, captured (the team's of the
flag: the one that defends it) */
static void bots_command_flag_events(void)
{
	short team;

	for (team = 0; team < 2; team++)
	{
		long object_index, carrier;
		real_point3d position;
		boolean at_home;
		char const *name = NULL;

		if (!bots_command_flag_state(team, &object_index, &position, &carrier, &at_home))
		{
			bots_command.flags_known = FALSE;
			continue;
		}
		if (bots_command.flags_known)
		{
			long before = bots_command.flag_carriers[team];

			if (carrier != NONE && before == NONE)
				name = "flag_taken";
			else if (carrier == NONE && before != NONE)
				name = at_home ? "flag_captured" : "flag_dropped";
			else if (carrier == NONE && at_home && !bots_command.flag_home[team])
				name = "flag_returned";
			if (name)
			{
				struct server_json json;

				bots_command_event_begin(&json, name);
				server_json_string(&json, "flag", bots_command_team_names[team]);
				bots_command_json_player(&json, "player", carrier != NONE ? carrier : before);
				/* (none the tick it was dropped, out of its carrier's hands) */
				if (position.x != 0.0f || position.y != 0.0f || position.z != 0.0f)
					bots_command_json_point(&json, "position", &position);
				bots_command_event_end(&json);
			}
		}
		bots_command.flag_carriers[team] = carrier;
		bots_command.flag_home[team] = at_home;
	}
	bots_command.flags_known = TRUE;
	for (team = 0; team < 2; team++)
	{
		long object_index, carrier;
		real_point3d position;
		boolean at_home;

		if (!bots_command_flag_state(team, &object_index, &position, &carrier, &at_home))
			bots_command.flags_known = FALSE;
	}
}

/* ---------- traits */

static void bots_command_traits_default(struct bots_command_traits *traits)
{
	csmemset(traits, 0, sizeof(*traits));
	traits->skill = NONE;
	traits->aggression = 0.5f;
	traits->teamwork = 1.0f;
	traits->vehicles = BOTS_COMMAND_ALL_VEHICLES;
	traits->style = 0;
}

static void bots_command_traits_ready(void)
{
	long index;

	if (bots_command.traits_ready)
		return;
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		bots_command_traits_default(&bots_command.traits[index]);
		bots_command.join_teams[index] = NONE;
	}
	bots_command.traits_ready = TRUE;
}

/* a trait set (its value checked by bots_command_parse) */
static void bots_command_trait_set(struct bots_command_traits *traits, short trait, char const *value)
{
	switch (trait)
	{
	case _bots_trait_skill:
		traits->skill = bots_command_name_index(value, bots_command_skill_names, 4);
		break;
	case _bots_trait_aggression:
		traits->aggression = (real)strtod(value, NULL);
		break;
	case _bots_trait_teamwork:
		traits->teamwork = (real)strtod(value, NULL);
		break;
	case _bots_trait_weapon:
		if (!csstrcasecmp(value, "any") || !csstrcasecmp(value, "none") || !csstrcasecmp(value, "default"))
			traits->weapon[0] = 0;
		else
		{
			long index;

			/* (the tag's words: "sniper rifle", its _ as spaces) */
			snprintf(traits->weapon, sizeof(traits->weapon), "%s", value);
			for (index = 0; traits->weapon[index]; index++)
			{
				traits->weapon[index] = (char)tolower((unsigned char)traits->weapon[index]);
				if (traits->weapon[index] == '_')
					traits->weapon[index] = ' ';
			}
		}
		break;
	case _bots_trait_vehicles:
		if (!csstrcasecmp(value, "all") || !csstrcasecmp(value, "any"))
			traits->vehicles = BOTS_COMMAND_ALL_VEHICLES;
		else
		{
			char copy[BOTS_COMMAND_LINE_SIZE];
			char *part;

			traits->vehicles = 0;
			snprintf(copy, sizeof(copy), "%s", value);
			for (part = strtok(copy, ", "); part && csstrcasecmp(part, "none"); part = strtok(NULL, ", "))
			{
				short kind = bots_command_name_index(part, bots_command_vehicle_names, NUMBER_OF_BOTS_VEHICLE_KINDS - 1);

				if (kind != NONE)
					traits->vehicles |= 1UL << kind;
			}
		}
		break;
	case _bots_trait_style:
	{
		short style;

		for (style = 0; style < NUMBER_OF_BOTS_COMMAND_STYLES; style++)
		{
			if (!csstrcasecmp(value, bots_command_styles[style].name))
			{
				traits->style = style;
				traits->aggression = bots_command_styles[style].aggression;
				traits->teamwork = bots_command_styles[style].teamwork;
			}
		}
		break;
	}
	case _bots_trait_reset:
		bots_command_traits_default(traits);
		break;
	default:
		break;
	}
}

short bots_command_skill(long bot_index)
{
	bots_command_traits_ready();
	return VALID_INDEX(bot_index, BOTS_MAXIMUM) ? bots_command.traits[bot_index].skill : NONE;
}

real bots_command_aggression(long bot_index)
{
	bots_command_traits_ready();
	return VALID_INDEX(bot_index, BOTS_MAXIMUM) ? bots_command.traits[bot_index].aggression : 0.5f;
}

real bots_command_teamwork(long bot_index)
{
	bots_command_traits_ready();
	return VALID_INDEX(bot_index, BOTS_MAXIMUM) ? bots_command.traits[bot_index].teamwork : 1.0f;
}

char const *bots_command_weapon(long bot_index)
{
	bots_command_traits_ready();
	return VALID_INDEX(bot_index, BOTS_MAXIMUM) ? bots_command.traits[bot_index].weapon : "";
}

boolean bots_command_vehicle_allowed(long bot_index, short vehicle_kind)
{
	bots_command_traits_ready();
	if (!VALID_INDEX(bot_index, BOTS_MAXIMUM) || vehicle_kind < 0 || vehicle_kind >= 32)
		return TRUE;
	return (bots_command.traits[bot_index].vehicles & (1UL << vehicle_kind)) != 0;
}

short bots_command_join_team(long bot_index)
{
	bots_command_traits_ready();
	return VALID_INDEX(bot_index, BOTS_MAXIMUM) ? bots_command.join_teams[bot_index] : NONE;
}

/* ---------- orders */

/* the bots of who ("all", "red", "blue", a name, a number from 1; with
commas), in the game now: their bot numbers' bits; 0 (why, in error) for none */
static long bots_command_select(char const *who, boolean *selected, char *error, long error_size)
{
	char copy[BOTS_COMMAND_LINE_SIZE];
	char *part;
	long found = 0, index;

	csmemset(selected, 0, sizeof(*selected) * BOTS_MAXIMUM);
	snprintf(copy, sizeof(copy), "%s", who);
	for (part = strtok(copy, ","); part; part = strtok(NULL, ","))
	{
		short team = bots_command_team_word(part);
		boolean all = !csstrcasecmp(part, "all") || !strcmp(part, "*");
		char *end;
		long number = strtol(part[0] == '#' ? part + 1 : part, &end, 10);
		boolean numbered = end != part && !*end && team == NONE;
		long matched = 0;

		for (index = 0; index < BOTS_MAXIMUM; index++)
		{
			long player_index = bots_player_of_bot(index);
			struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
			char name[16];
			boolean match;

			if (!player)
				continue;
			bots_command_player_name(player, name, sizeof(name));
			match = all || (team != NONE && player->team_index == team) || (numbered && number == index + 1) ||
				(!numbered && team == NONE && !csstrcasecmp(name, part));
			if (match)
			{
				found += !selected[index];
				selected[index] = TRUE;
				matched++;
			}
		}
		if (!matched)
		{
			snprintf(error, (size_t)error_size, "no bot in the game is \"%s\" (bots lists them)", part);
			return 0;
		}
	}
	return found;
}

/* the order given to a bot (at the tick): its target found from its words
for the bot's team. FALSE, why in error, for none */
static boolean bots_command_order_give(long bot_index, short kind, char const *words, char *error, long error_size)
{
	struct bots_command_order order;
	long player_index = bots_player_of_bot(bot_index);
	struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
	short team = player && game_engine_has_teams() && VALID_INDEX(player->team_index, 2) ? (short)player->team_index :
		(short)NONE;
	char const *text = words;

	if (!player)
	{
		snprintf(error, (size_t)error_size, "the bot is not in the game");
		return FALSE;
	}
	csmemset(&order, 0, sizeof(order));
	order.kind = kind;
	order.state = _bots_order_state_active;
	order.given_tick = game_time_get();
	snprintf(order.words, sizeof(order.words), "%s", words);
	/* (each order's own target, else its default) */
	if (kind == _bots_command_push || kind == _bots_command_retreat)
		text = !*words ? (kind == _bots_command_push ? "enemy base" : "our base") : words;
	else if (kind == _bots_command_attack && !*words)
		text = "enemy base";
	else if (kind == _bots_command_defend && !*words)
	{
		long object_index, carrier;
		real_point3d position;
		boolean at_home;

		text = team != NONE && bots_command_flag_state(team, &object_index, &position, &carrier, &at_home) ?
			"our flag" : "our base";
	}
	if (kind != _bots_command_free && kind != _bots_command_regroup && *text &&
		!bots_command_resolve(kind, team, text, &order.target, error, error_size))
	{
		return FALSE;
	}
	switch (kind)
	{
	case _bots_command_hold:
		/* (no place: where it stands) */
		if (order.target.kind == _bots_target_none)
		{
			if (!bots_command_live_player(player_index, &order.target.point))
			{
				snprintf(error, (size_t)error_size, "the bot is dead: hold needs a place");
				return FALSE;
			}
			order.target.kind = _bots_target_point;
			snprintf(order.target.name, sizeof(order.target.name), "where it stood");
		}
		break;
	case _bots_command_regroup:
		if (!bots_command_team_middle(team, &order.target.point))
		{
			snprintf(error, (size_t)error_size, "the team has no one alive to regroup with");
			return FALSE;
		}
		order.target.kind = _bots_target_point;
		snprintf(order.target.name, sizeof(order.target.name), "the team's middle");
		break;
	case _bots_command_follow:
	{
		struct player_datum *leader = order.target.kind == _bots_target_player ?
			player_try_and_get(order.target.player_index) : NULL;

		if (!leader || order.target.player_index == player_index ||
			(team != NONE && leader->team_index != team))
		{
			snprintf(error, (size_t)error_size, "follow takes a teammate");
			return FALSE;
		}
		break;
	}
	default:
		break;
	}
	if (order.target.kind == _bots_target_point && kind != _bots_command_free && !bots_point_on_ground(&order.target.point))
	{
		snprintf(error, (size_t)error_size, "the bots find no ground at %s", order.target.name);
		return FALSE;
	}
	bots_command.orders[bot_index] = order;
	return TRUE;
}

/* an objective about a point */
static void bots_command_set_objective(struct bots_objective *objective, short kind, real_point3d const *point,
	real radius, boolean press_on)
{
	csmemset(objective, 0, sizeof(*objective));
	objective->kind = kind;
	objective->urgent = TRUE;
	objective->press_on = press_on;
	objective->point = *point;
	objective->object_index = NONE;
	objective->player_index = NONE;
	objective->radius = radius;
}

static boolean bots_command_order_fail(long bot_index, char const *reason)
{
	bots_command.orders[bot_index].state = _bots_order_state_failed;
	bots_command_order_event(bot_index, "order_failed", reason);
	bots_command.orders[bot_index].kind = _bots_command_free;
	return FALSE;
}

boolean bots_command_objective(long bot_index, long player_index, real_point3d const *position,
	struct bots_objective *objective)
{
	struct bots_command_order *order = VALID_INDEX(bot_index, BOTS_MAXIMUM) ? &bots_command.orders[bot_index] : NULL;
	struct player_datum *player = player_get(player_index);
	real_point3d point;
	boolean moving;

	if (!order || order->kind == _bots_command_free)
		return FALSE;
	/* (a flag or ball in its hands: the game's play, home with it) */
	if (bots_objectives_carrying(player_index))
	{
		if (order->kind == _bots_command_attack && order->state == _bots_order_state_active &&
			order->target.kind == _bots_target_object)
		{
			order->state = _bots_order_state_done;
			bots_command_order_event(bot_index, "order_done", "taken");
		}
		return FALSE;
	}
	point = order->target.point;
	switch (order->target.kind)
	{
	case _bots_target_object:
	{
		struct object_datum *object = object_try_and_get(order->target.object_index);

		if (!object || TEST_FLAG(object->object.damage_flags, _object_dead_bit))
			return bots_command_order_fail(bot_index, "it is gone");
		point = object->object.position;
		/* (a flag or ball carried: after the enemy carrying it, with the
		teammate who is, as the HUD shows them) */
		if (object->object.parent_object_index != NONE && TEST_FLAG(_object_mask_weapon, object->object.type))
		{
			struct object_datum *carrier = object_try_and_get(object->object.parent_object_index);
			long carrier_player = carrier && TEST_FLAG(_object_mask_unit, carrier->object.type) ?
				unit_get(object->object.parent_object_index)->unit.player_index : NONE;
			struct player_datum *carrying = carrier_player != NONE ? player_try_and_get(carrier_player) : NULL;

			if (!carrying)
				return FALSE;
			if (game_engine_has_teams() && carrying->team_index == player->team_index)
			{
				bots_command_set_objective(objective, _bots_objective_escort, &carrier->object.position,
					BOTS_COMMAND_FOLLOW_RADIUS, TRUE);
				objective->player_index = carrier_player;
				return TRUE;
			}
			bots_command_set_objective(objective, _bots_objective_hunt, &carrier->object.position, 2.0f, TRUE);
			objective->player_index = carrier_player;
			return TRUE;
		}
		/* (a flag lying: taken, or touched to return it) */
		if (TEST_FLAG(_object_mask_weapon, object->object.type) && weapon_is_flag(order->target.object_index) &&
			(order->kind == _bots_command_attack || order->kind == _bots_command_push ||
				order->kind == _bots_command_goto))
		{
			bots_command_set_objective(objective, _bots_objective_take, &point, 1.0f, TRUE);
			objective->object_index = order->target.object_index;
			return TRUE;
		}
		break;
	}
	case _bots_target_player:
	{
		struct player_datum *other = player_try_and_get(order->target.player_index);
		real_point3d where;

		if (!other || other->quit_out_of_game)
			return bots_command_order_fail(bot_index, "the player left");
		/* (dead: on with its own play until they are back) */
		if (!bots_command_live_player(order->target.player_index, &where))
			return FALSE;
		if (!game_engine_has_teams() || other->team_index != player->team_index)
		{
			/* (an enemy: where its team last knew of them, else its own
			search) */
			struct bots_known_enemy known[BOTS_KNOWLEDGE_MAXIMUM_KNOWN];
			short count = bots_knowledge_known(bot_index, known, BOTS_KNOWLEDGE_MAXIMUM_KNOWN), index;

			for (index = 0; index < count; index++)
			{
				if (known[index].player_index == order->target.player_index)
				{
					bots_command_set_objective(objective, _bots_objective_hunt, &known[index].position, 2.0f, TRUE);
					objective->player_index = order->target.player_index;
					return TRUE;
				}
			}
			return FALSE;
		}
		/* (a teammate: with them) */
		bots_command_set_objective(objective, _bots_objective_escort, &where,
			order->kind == _bots_command_defend || order->kind == _bots_command_hold ? 2.0f : BOTS_COMMAND_FOLLOW_RADIUS,
			TRUE);
		objective->player_index = order->target.player_index;
		return TRUE;
	}
	case _bots_target_point:
		break;
	default:
		return FALSE;
	}
	moving = order->kind == _bots_command_attack || order->kind == _bots_command_push ||
		order->kind == _bots_command_goto || order->kind == _bots_command_retreat || order->kind == _bots_command_regroup;
	if (moving && order->state == _bots_order_state_active)
	{
		real dx = point.x - position->x, dy = point.y - position->y;

		if (dx * dx + dy * dy < BOTS_COMMAND_ARRIVE_REACH * BOTS_COMMAND_ARRIVE_REACH &&
			fabs(point.z - position->z) < BOTS_COMMAND_ARRIVE_HEIGHT)
		{
			order->state = _bots_order_state_done;
			bots_command_order_event(bot_index, "order_done", "there");
		}
		else
		{
			bots_command_set_objective(objective, _bots_objective_take, &point, 1.0f, TRUE);
			return TRUE;
		}
	}
	switch (order->kind)
	{
	case _bots_command_hold:
		bots_command_set_objective(objective, _bots_objective_guard, &point, BOTS_COMMAND_HOLD_RADIUS, TRUE);
		break;
	case _bots_command_goto:
	case _bots_command_regroup:
		bots_command_set_objective(objective, _bots_objective_guard, &point, BOTS_COMMAND_STAY_RADIUS, FALSE);
		break;
	default:
		/* (a defense; an attack, a push, a retreat there: holding the
		ground about it) */
		bots_command_set_objective(objective, _bots_objective_guard, &point, BOTS_COMMAND_DEFEND_RADIUS, FALSE);
		break;
	}
	return TRUE;
}

real bots_command_target_bonus(long bot_index, long target_player_index)
{
	struct bots_command_order const *order = VALID_INDEX(bot_index, BOTS_MAXIMUM) ? &bots_command.orders[bot_index] :
		NULL;

	return order && order->kind == _bots_command_attack && order->target.kind == _bots_target_player &&
		order->target.player_index == target_player_index ? BOTS_COMMAND_TARGET_BONUS : 0.0f;
}

/* ---------- applying a line at the tick */

/* a queued line applied to the bots it names: an order, a trait, a team
move; what it did, as an event */
static void bots_command_apply(char const *line)
{
	struct bots_command_request request;
	boolean selected[BOTS_MAXIMUM];
	char error[160];
	struct server_json json;
	long index, applied = 0;

	/* (the demo's, as it was applied: demo.c) */
	demo_record_bot_command(line);
	if (!bots_command_parse(line, &request, error, sizeof(error)) ||
		!bots_command_select(request.who, selected, error, sizeof(error)))
	{
		bots_command_event_begin(&json, "command_failed");
		server_json_string(&json, "line", line);
		server_json_string(&json, "reason", error);
		bots_command_event_end(&json);
		return;
	}
	error[0] = 0;
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		if (!selected[index])
			continue;
		switch (request.kind)
		{
		case _bots_request_order:
			if (bots_command_order_give(index, request.order, request.target, error, sizeof(error)))
			{
				applied++;
				if (request.order != _bots_command_free)
					bots_command_order_event(index, "order", NULL);
			}
			else
			{
				struct server_json failed;

				bots_command_event_begin(&failed, "order_failed");
				bots_command_json_player(&failed, "bot", bots_player_of_bot(index));
				server_json_integer(&failed, "number", index + 1);
				server_json_string(&failed, "order", bots_command_order_names[request.order]);
				server_json_string(&failed, "target", request.target);
				server_json_string(&failed, "reason", error);
				bots_command_event_end(&failed);
			}
			break;
		case _bots_request_set:
			bots_command_traits_ready();
			bots_command_trait_set(&bots_command.traits[index], request.trait, request.value);
			applied++;
			break;
		case _bots_request_team:
		{
			char notice[128];
			long player_index = bots_player_of_bot(index);

			if (player_index != NONE && player_get(player_index)->team_index == request.team)
				applied++;
			else if (player_index != NONE && server_team_switch(player_index, NULL, notice, sizeof(notice)))
				applied++;
			else
				snprintf(error, sizeof(error), "%s", player_index != NONE ? notice : "the bot is not in the game");
			break;
		}
		default:
			break;
		}
	}
	bots_command_event_begin(&json, "command");
	server_json_string(&json, "line", line);
	server_json_integer(&json, "bots", applied);
	if (error[0])
		server_json_string(&json, "reason", error);
	bots_command_event_end(&json);
}

/* a line kept for the next tick (bots_command_tick_begin), after those
before it; FALSE when the queue is full */
static boolean bots_command_enqueue(char const *line)
{
	if (bots_command.queue_count >= BOTS_COMMAND_QUEUE)
		return FALSE;
	snprintf(bots_command.queue[(bots_command.queue_first + bots_command.queue_count) % BOTS_COMMAND_QUEUE],
		BOTS_COMMAND_LINE_SIZE, "%s", line);
	bots_command.queue_count++;
	return TRUE;
}

void bots_command_tick_begin(void)
{
	bots_command_traits_ready();
	while (bots_command.queue_count > 0)
	{
		char line[BOTS_COMMAND_LINE_SIZE];

		snprintf(line, sizeof(line), "%s", bots_command.queue[bots_command.queue_first]);
		bots_command.queue_first = (bots_command.queue_first + 1) % BOTS_COMMAND_QUEUE;
		bots_command.queue_count--;
		bots_command_apply(line);
	}
	bots_command_flag_events();
}

void bots_command_reset(void)
{
	long index;

	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		csmemset(&bots_command.orders[index], 0, sizeof(bots_command.orders[index]));
		bots_command.orders[index].kind = _bots_command_free;
	}
	bots_command.flags_known = FALSE;
}

/* ---------- answers */

/* the reply's text, kept within its size */
struct bots_command_reply
{
	char *text;
	long size;
	long length;
};

static void bots_command_say(struct bots_command_reply *reply, char const *format, ...)
{
	va_list arguments;
	int written;

	if (reply->length >= reply->size - 1)
		return;
	va_start(arguments, format);
	written = vsnprintf(reply->text + reply->length, (size_t)(reply->size - reply->length), format, arguments);
	va_end(arguments);
	if (written < 0)
		return;
	reply->length = MIN(reply->length + written, reply->size - 1);
	if (reply->length < reply->size - 1)
		reply->text[reply->length++] = '\n';
	reply->text[reply->length] = 0;
}

/* a JSON reply's text as its one line */
static void bots_command_say_json(struct bots_command_reply *reply, struct server_json *json)
{
	char const *text = server_json_text(json);

	if ((long)strlen(text) >= reply->size - reply->length - 1)
		bots_command_say(reply, "{\"ok\":false,\"error\":\"the answer is too long\"}");
	else
		bots_command_say(reply, "%s", text);
	server_json_free(json);
}

static char const *bots_command_engine_name(long engine)
{
	static char const *const names[] = { "none", "ctf", "slayer", "oddball", "king", "race" };

	return engine >= 0 && engine < (long)NUMBEROF(names) ? names[engine] : "other";
}

static void bots_command_status(struct bots_command_reply *reply, boolean json)
{
	boolean playing = bots_command_game();
	struct game_variant *variant = playing ? game_engine_get_variant() : NULL;
	char map[64] = "", variant_name[16] = "";
	long seconds = playing ? game_time_get() / TICKS_PER_SECOND : 0;
	long limit = playing ? game_variant_options_get()->time_limit * 60L : 0;
	boolean teams = playing && game_engine_has_teams();
	long bots = 0, players = 0, index;
	struct data_iterator iterator;
	struct player_datum *player;

	if (playing && global_scenario_index != NONE)
	{
		char const *name = tag_get_name(global_scenario_index);
		char const *part = name ? strrchr(name, '\\') : NULL;

		snprintf(map, sizeof(map), "%s", part ? part + 1 : name ? name : "");
	}
	for (index = 0; variant && index < 11 && variant->human_readable_game_description[index]; index++)
		variant_name[index] = player_name_character_ascii(variant->human_readable_game_description[index]);
	variant_name[variant ? index : 0] = 0;
	if (playing)
	{
		data_iterator_new(&iterator, player_data);
		while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
		{
			if (player->quit_out_of_game)
				continue;
			players++;
			bots += bots_player_is_bot(iterator.datum_index) != 0;
		}
	}
	if (json)
	{
		struct server_json out;

		server_json_begin(&out);
		server_json_object_begin(&out, NULL);
		server_json_boolean(&out, "ok", TRUE);
		server_json_string(&out, "state", playing ? "playing" : "not playing");
		server_json_string(&out, "map", map);
		server_json_string(&out, "mode", variant ? bots_command_engine_name(variant->game_engine_index) : "");
		server_json_string(&out, "variant", variant_name);
		server_json_boolean(&out, "teams", teams);
		if (teams)
		{
			server_json_array_begin(&out, "score");
			server_json_integer(&out, NULL, game_engine_get_team_score(0));
			server_json_integer(&out, NULL, game_engine_get_team_score(1));
			server_json_array_end(&out);
		}
		else if (playing)
		{
			server_json_array_begin(&out, "score");
			data_iterator_new(&iterator, player_data);
			while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
			{
				char name[16];

				if (player->quit_out_of_game)
					continue;
				bots_command_player_name(player, name, sizeof(name));
				server_json_object_begin(&out, NULL);
				server_json_string(&out, "name", name);
				server_json_integer(&out, "score", game_engine && game_engine->get_player_score ?
					game_engine->get_player_score(DATUM_INDEX_TO_ABSOLUTE_INDEX(iterator.datum_index), _get_score_individual) :
					0);
				server_json_object_end(&out);
			}
			server_json_array_end(&out);
		}
		else
			server_json_null(&out, "score");
		server_json_integer(&out, "seconds", seconds);
		server_json_integer(&out, "time_limit", limit);
		server_json_integer(&out, "tick", playing ? game_time_get() : 0);
		server_json_integer(&out, "players", players);
		server_json_integer(&out, "bots", bots);
		server_json_integer(&out, "bots_wanted", bots_wanted_count());
		server_json_integer(&out, "queued", bots_command.queue_count);
		server_json_integer(&out, "next_event", bots_command.next_event);
		server_json_object_end(&out);
		bots_command_say_json(reply, &out);
		return;
	}
	if (!playing)
	{
		bots_command_say(reply, "No game is being played (the lobby or the menus); %ld bots wanted.", bots_wanted_count());
		return;
	}
	bots_command_say(reply, "%s on %s (%s), %ld:%02ld played%s", variant_name[0] ? variant_name : "A game", map,
		bots_command_engine_name(variant->game_engine_index), seconds / 60, seconds % 60,
		limit ? "" : ", no time limit");
	if (limit)
		bots_command_say(reply, "Time limit %ld:%02ld, %ld:%02ld left", limit / 60, limit % 60,
			MAX(limit - seconds, 0) / 60, MAX(limit - seconds, 0) % 60);
	if (teams)
		bots_command_say(reply, "Score: red %ld, blue %ld", game_engine_get_team_score(0), game_engine_get_team_score(1));
	bots_command_say(reply, "%ld players, %ld of them bots", players, bots);
}

static void bots_command_bots(struct bots_command_reply *reply, boolean json)
{
	struct server_json out;
	long index, shown = 0;
	boolean playing = bots_command_game();

	if (json)
	{
		server_json_begin(&out);
		server_json_object_begin(&out, NULL);
		server_json_boolean(&out, "ok", TRUE);
		server_json_integer(&out, "tick", playing ? game_time_get() : 0);
		server_json_array_begin(&out, "bots");
	}
	for (index = 0; playing && index < BOTS_MAXIMUM; index++)
	{
		long player_index = bots_player_of_bot(index);
		struct player_datum *player = player_index != NONE ? player_get(player_index) : NULL;
		struct bots_command_order const *order = &bots_command.orders[index];
		struct bots_command_traits const *traits = &bots_command.traits[index];
		struct bots_bot_view view;
		struct object_datum *object = NULL;
		real_point3d where = { 0.0f, 0.0f, 0.0f };
		char name[16], target[16] = "", weapon[BOTS_COMMAND_NAME_SIZE] = "", doing[64];
		char const *team = "";

		if (!player)
			continue;
		bots_command_traits_ready();
		bots_command_player_name(player, name, sizeof(name));
		if (game_engine_has_teams() && VALID_INDEX(player->team_index, 2))
			team = bots_command_team_names[player->team_index];
		if (player->unit_index != NONE)
		{
			struct unit_datum *unit = unit_get(player->unit_index);
			long weapon_index = unit->unit.current_weapon_index != NONE ?
				unit_inventory_get_weapon(player->unit_index, unit->unit.current_weapon_index) : NONE;
			struct object_datum *held = weapon_index != NONE ? object_try_and_get(weapon_index) : NULL;
			char const *part = held ? bots_command_tag_part(held->definition_index) : NULL;

			object = object_get(player->unit_index);
			bots_command_unit_position(player->unit_index, &where);
			snprintf(weapon, sizeof(weapon), "%s", part ? part : "");
		}
		bots_bot_view(index, &view);
		if (view.target_player_index != NONE && player_try_and_get(view.target_player_index))
			bots_command_player_name(player_get(view.target_player_index), target, sizeof(target));
		if (!object)
			snprintf(doing, sizeof(doing), "dead");
		else if (view.fighting)
			snprintf(doing, sizeof(doing), "fighting %s", target);
		else if (view.in_vehicle)
			snprintf(doing, sizeof(doing), "in a vehicle");
		else
			snprintf(doing, sizeof(doing), "%s", view.goal >= 0 && view.goal < (short)NUMBEROF(bots_command_goal_names) ?
				bots_command_goal_names[view.goal] : "");
		if (json)
		{
			server_json_object_begin(&out, NULL);
			server_json_integer(&out, "number", index + 1);
			server_json_string(&out, "name", name);
			server_json_string(&out, "team", team);
			server_json_boolean(&out, "alive", object != NULL);
			if (object)
			{
				bots_command_json_point(&out, "position", &where);
				server_json_real(&out, "health", floor(object->object.body_vitality * 100.0 + 0.5) / 100.0);
				server_json_real(&out, "shield", floor(object->object.shield_vitality * 100.0 + 0.5) / 100.0);
			}
			server_json_string(&out, "weapon", weapon);
			server_json_integer(&out, "kills", player->statistics.kills[0]);
			server_json_integer(&out, "deaths", player->statistics.deaths);
			server_json_object_begin(&out, "order");
			server_json_string(&out, "kind", bots_command_order_names[order->kind]);
			server_json_string(&out, "target", order->target.name);
			server_json_string(&out, "state", bots_command_order_state_names[order->state]);
			server_json_object_end(&out);
			server_json_object_begin(&out, "traits");
			server_json_string(&out, "skill", traits->skill != NONE ? bots_command_skill_names[traits->skill] : "default");
			server_json_real(&out, "aggression", traits->aggression);
			server_json_real(&out, "teamwork", traits->teamwork);
			server_json_string(&out, "weapon", traits->weapon);
			if (traits->vehicles == BOTS_COMMAND_ALL_VEHICLES)
				server_json_string(&out, "vehicles", "all");
			else
			{
				short kind;

				server_json_array_begin(&out, "vehicles");
				for (kind = _bots_vehicle_jeep; kind < _bots_vehicle_other; kind++)
				{
					if (traits->vehicles & (1UL << kind))
						server_json_string(&out, NULL, bots_command_vehicle_names[kind]);
				}
				server_json_array_end(&out);
			}
			server_json_string(&out, "style", bots_command_styles[traits->style].name);
			server_json_object_end(&out);
			server_json_string(&out, "doing", doing);
			if (view.fighting && target[0])
				server_json_string(&out, "fighting", target);
			server_json_object_end(&out);
		}
		else
		{
			char order_text[96];

			if (order->kind == _bots_command_free)
				snprintf(order_text, sizeof(order_text), "no order");
			else
				snprintf(order_text, sizeof(order_text), "%s %s%s", bots_command_order_names[order->kind], order->target.name,
					order->state == _bots_order_state_done ? " (there)" : "");
			if (object)
			{
				bots_command_say(reply, "%2ld %-11s %-4s %7.1f %7.1f %6.1f  %3.0f%%/%3.0f%%  %-16s %-26s %s", index + 1, name,
					team, where.x, where.y, where.z,
					object->object.body_vitality * 100.0f, object->object.shield_vitality * 100.0f, weapon, order_text, doing);
			}
			else
				bots_command_say(reply, "%2ld %-11s %-4s %-46s %-26s dead", index + 1, name, team, "", order_text);
			if (traits->skill != NONE || traits->aggression != 0.5f || traits->teamwork != 1.0f || traits->weapon[0] ||
				traits->vehicles != BOTS_COMMAND_ALL_VEHICLES)
			{
				bots_command_say(reply, "   traits: skill %s, aggression %.2f, teamwork %.2f%s%s%s", traits->skill != NONE ?
					bots_command_skill_names[traits->skill] : "default", traits->aggression, traits->teamwork,
					traits->weapon[0] ? ", weapon " : "", traits->weapon, traits->vehicles != BOTS_COMMAND_ALL_VEHICLES ?
					", some vehicles" : "");
			}
		}
		shown++;
	}
	if (json)
	{
		server_json_array_end(&out);
		server_json_object_end(&out);
		bots_command_say_json(reply, &out);
	}
	else if (!shown)
		bots_command_say(reply, playing ? "No bots are in the game (bots add)." : "No game is being played.");
}

static void bots_command_places_answer(struct bots_command_reply *reply, boolean json)
{
	struct bots_command_place places[BOTS_COMMAND_PLACES];
	long count = bots_command_game() ? bots_command_places(places, BOTS_COMMAND_PLACES) : 0, index;
	struct server_json out;

	if (json)
	{
		server_json_begin(&out);
		server_json_object_begin(&out, NULL);
		server_json_boolean(&out, "ok", TRUE);
		server_json_array_begin(&out, "places");
		for (index = 0; index < count; index++)
		{
			server_json_object_begin(&out, NULL);
			server_json_string(&out, "name", places[index].name);
			server_json_string(&out, "kind", places[index].kind);
			if (VALID_INDEX(places[index].team, 2))
				server_json_string(&out, "team", bots_command_team_names[places[index].team]);
			bots_command_json_point(&out, "position", &places[index].point);
			server_json_object_end(&out);
		}
		server_json_array_end(&out);
		server_json_object_end(&out);
		bots_command_say_json(reply, &out);
		return;
	}
	if (!count)
		bots_command_say(reply, "No game is being played, or its map names no places.");
	for (index = 0; index < count; index++)
	{
		bots_command_say(reply, "%-22s %-8s %8.1f %8.1f %7.1f", places[index].name, places[index].kind,
			places[index].point.x, places[index].point.y, places[index].point.z);
	}
}

static void bots_command_events_answer(struct bots_command_reply *reply, long after, boolean json)
{
	long first = MAX(bots_command.next_event - BOTS_COMMAND_EVENTS, 0);
	long start = MAX(after, first), number;
	struct server_json out;

	server_json_begin(&out);
	server_json_object_begin(&out, NULL);
	server_json_boolean(&out, "ok", TRUE);
	server_json_array_begin(&out, "events");
	for (number = start; number < bots_command.next_event; number++)
	{
		char const *text = bots_command.events[number % BOTS_COMMAND_EVENTS];

		/* (the reply's room kept for the rest: the next request reads on) */
		if (!text || out.length + (long)strlen(text) + 64 >= reply->size - reply->length - 1)
			break;
		if (json)
			server_json_raw(&out, NULL, text);
		else
			bots_command_say(reply, "%s", text);
	}
	server_json_array_end(&out);
	server_json_integer(&out, "next", number);
	server_json_integer(&out, "missed", after < first ? first - after : 0);
	server_json_object_end(&out);
	if (json)
		bots_command_say_json(reply, &out);
	else
		server_json_free(&out);
}

static void bots_command_help(struct bots_command_reply *reply)
{
	static char const *const lines[] =
	{
		"bots status                         the game: map, mode, score, time",
		"bots bots                           each bot: team, place, health, weapon, order, traits, doing",
		"bots watch                          the events, one JSON a line (kills, flags, orders), and snapshots",
		"bots places                         the map's places orders can name",
		"bots order <who> <order> [target]   orders: attack, defend, hold, follow, push, retreat, regroup,",
		"                                    goto, clear; a target is a place, a player, me, or x,y,z",
		"bots set <who> <trait> <value>      skill easy|normal|heroic|legendary|default, aggression 0-1,",
		"                                    teamwork 0-1, weapon <name>|any, vehicles all|none|ghost,...,",
		"                                    style default|rusher|defender|lone; set <who> reset",
		"bots add [n] [red|blue]  remove [n]  fill <n>  team <who> <red|blue>",
		"<who>: all, red, blue, a bot's name or number, or several with commas (Johnson,Jenkins)",
		"--json: one JSON object a line, for scripts and agents",
		"Examples:",
		"  bots order red defend red flag",
		"  bots order all red attack blue base",
		"  bots order Johnson follow me",
		"  bots order blue goto warthog 1",
		"  bots set blue aggression 0.9",
		"  bots set all skill legendary",
	};
	long index;

	for (index = 0; index < (long)NUMBEROF(lines); index++)
		bots_command_say(reply, "%s", lines[index]);
}

/* the add, remove and fill: bots.count (and bots.fill_to, bots.make_room)
for this run, which the lobby and the game follow (network_server_manager.c) */
static boolean bots_command_count(struct bots_command_request const *request, struct bots_command_reply *reply,
	char *error, long error_size)
{
	long count = bots_wanted_count(), index;
	char text[16];

	if (bots_matchmaking_on() || bots_training_on())
	{
		snprintf(error, (size_t)error_size, "this game's bots are its playlist's or Basic Training's");
		return FALSE;
	}
	switch (request->kind)
	{
	case _bots_request_add:
		/* (the team the next ones join: those numbered from the count on) */
		bots_command_traits_ready();
		for (index = count; index < count + request->count && index < BOTS_MAXIMUM; index++)
			bots_command.join_teams[index] = request->team;
		count = MIN(count + request->count, (long)BOTS_MAXIMUM);
		snprintf(text, sizeof(text), "%ld", count);
		config_set_for_run("bots.make_room", "false");
		config_set_for_run("bots.count", text);
		bots_command_say(reply, "%ld bots wanted (they join as the game has room)", count);
		return TRUE;
	case _bots_request_remove:
		count = MAX(count - request->count, 0L);
		snprintf(text, sizeof(text), "%ld", count);
		config_set_for_run("bots.make_room", "false");
		config_set_for_run("bots.count", text);
		bots_command_say(reply, "%ld bots wanted (the game takes out those of the larger team first)", count);
		return TRUE;
	case _bots_request_fill:
		snprintf(text, sizeof(text), "%ld", request->count);
		config_set_for_run("bots.count", "64");
		config_set_for_run("bots.fill_to", text);
		config_set_for_run("bots.make_room", "true");
		bots_command_say(reply, "the game filled to %ld players and bots", request->count);
		return TRUE;
	default:
		return FALSE;
	}
}

boolean bots_command_run(char const *line, char *reply_text, long reply_size)
{
	struct bots_command_request request;
	struct bots_command_reply reply;
	char error[200];
	boolean succeeded = TRUE;

	reply.text = reply_text;
	reply.size = reply_size;
	reply.length = 0;
	if (reply_size > 0)
		reply_text[0] = 0;
	bots_command_traits_ready();
	if (!bots_command_parse(line, &request, error, sizeof(error)))
		succeeded = FALSE;
	else if (game_connection() == _game_connection_network_client)
	{
		snprintf(error, sizeof(error), "only the game's host directs its bots");
		succeeded = FALSE;
	}
	else
	{
		switch (request.kind)
		{
		case _bots_request_help:
			bots_command_help(&reply);
			break;
		case _bots_request_status:
			bots_command_status(&reply, request.json);
			break;
		case _bots_request_bots:
			bots_command_bots(&reply, request.json);
			break;
		case _bots_request_places:
			bots_command_places_answer(&reply, request.json);
			break;
		case _bots_request_events:
			bots_command_events_answer(&reply, request.after, request.json);
			break;
		case _bots_request_add:
		case _bots_request_remove:
		case _bots_request_fill:
			if (!bots_command_count(&request, &reply, error, sizeof(error)))
				succeeded = FALSE;
			break;
		default:
		{
			boolean selected[BOTS_MAXIMUM];
			long count;

			/* (an order, a trait, a team: the bots named checked now, the
			rest at the tick) */
			if (!bots_command_game())
			{
				snprintf(error, sizeof(error), "no game is being played");
				succeeded = FALSE;
				break;
			}
			count = bots_command_select(request.who, selected, error, sizeof(error));
			if (!count)
			{
				succeeded = FALSE;
				break;
			}
			if (request.kind == _bots_request_order && request.order != _bots_command_free &&
				request.order != _bots_command_regroup && request.order != _bots_command_hold && request.target[0])
			{
				struct bots_command_target target;
				long index;
				short team = NONE;

				for (index = 0; index < BOTS_MAXIMUM && team == NONE; index++)
				{
					if (selected[index] && game_engine_has_teams())
						team = (short)player_get(bots_player_of_bot(index))->team_index;
				}
				if (!bots_command_resolve(request.order, team, request.target, &target, error, sizeof(error)))
				{
					succeeded = FALSE;
					break;
				}
			}
			if (!bots_command_enqueue(line))
			{
				snprintf(error, sizeof(error), "%d lines wait for the game's next tick already", BOTS_COMMAND_QUEUE);
				succeeded = FALSE;
				break;
			}
			if (request.json)
			{
				struct server_json out;

				server_json_begin(&out);
				server_json_object_begin(&out, NULL);
				server_json_boolean(&out, "ok", TRUE);
				server_json_integer(&out, "bots", count);
				server_json_integer(&out, "tick", game_time_get());
				server_json_integer(&out, "next_event", bots_command.next_event);
				server_json_object_end(&out);
				bots_command_say_json(&reply, &out);
			}
			else
				bots_command_say(&reply, "%ld bot%s, at the next tick", count, count == 1 ? "" : "s");
			break;
		}
		}
	}
	if (!succeeded)
	{
		if (request.json)
		{
			struct server_json out;

			server_json_begin(&out);
			server_json_object_begin(&out, NULL);
			server_json_boolean(&out, "ok", FALSE);
			server_json_string(&out, "error", error);
			server_json_object_end(&out);
			bots_command_say_json(&reply, &out);
		}
		else
			bots_command_say(&reply, "bots: %s", error);
	}
	return succeeded;
}

boolean bots_command_console(char const *expression, boolean *result)
{
	static char reply[16384];
	char const *line = expression;
	char *cursor;

	while (*line == ' ' || *line == '\t')
		line++;
	if (tolower((unsigned char)line[0]) != 'b' || tolower((unsigned char)line[1]) != 'o' ||
		tolower((unsigned char)line[2]) != 't' || tolower((unsigned char)line[3]) != 's' ||
		(line[4] && line[4] != ' ' && line[4] != '\t'))
	{
		return FALSE;
	}
	*result = bots_command_run(line + 4, reply, sizeof(reply));
	for (cursor = reply; *cursor;)
	{
		char *end = strchr(cursor, '\n');

		if (end)
			*end = 0;
		console_printf(FALSE, "%s", cursor);
		platform_log("bots: %s", cursor);
		if (!end)
			break;
		cursor = end + 1;
	}
	return TRUE;
}
