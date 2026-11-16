/*
BOTS_TEAM_VIEW.C (test)

A team's coach's view (bots_command.c's --team): what the team knows
(bots_command_team_json) lists its own players where they are, and of its
enemies only those its bots saw, where and when they last saw them
(bots_knowledge.c's bots_knowledge_team_known), never where they are now;
the watch's events (bots_command_events_answer) give it nothing of the
other team's orders and a kill it was not part of without where. The real
JSON writer (server_json.c) writes it. test_bots_team_view.py takes the
constants and structures (config.inc) and the code (under_test.inc).
*/

#include "harness.h"

#include <math.h>

#define csmemset memset
#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define VALID_INDEX(index, count) ((index) >= 0 && (index) < (count))
#define TICKS_PER_SECOND 30
#define BOTS_MAXIMUM 16
#define BOTS_KNOWLEDGE_TEAM_MEMORY 32
#define BOTS_KNOWLEDGE_NAME_SIZE 32

#include "server_json.inc"
#include "config.inc"

/* the game's players and their units, as the code reads them */
struct object_datum
{
	struct
	{
		real_point3d position;
		long parent_object_index;
		real body_vitality;
		real shield_vitality;
	} object;
	boolean exists;
};

struct player_datum
{
	boolean quit_out_of_game;
	long unit_index;
	long team_index;
	short name[12];
};

struct data_iterator
{
	long next;
	long datum_index;
};

struct bots_item
{
	short kind;
	boolean power;
	char name[BOTS_KNOWLEDGE_NAME_SIZE];
	real_point3d position;
	long period_ticks;
};

#define PLAYERS 4
static struct player_datum players[PLAYERS];
static struct object_datum objects[PLAYERS];
static void *player_data;
static char const *const bots_command_team_names[] = { "red", "blue" };

static struct
{
	long next_event;
	char *events[BOTS_COMMAND_EVENTS];
	long event_numbers[BOTS_COMMAND_EVENTS];
	unsigned long event_teams[BOTS_COMMAND_EVENTS];
	char *event_public[BOTS_COMMAND_EVENTS];
} bots_command;

static struct
{
	struct bots_knowledge_team_sighting team_seen[2][BOTS_KNOWLEDGE_TEAM_MEMORY];
} bots_knowledge;

static void data_iterator_new(struct data_iterator *iterator, void *data)
{
	(void)data;
	iterator->next = 0;
	iterator->datum_index = NONE;
}

static void *data_iterator_next(struct data_iterator *iterator)
{
	if (iterator->next >= PLAYERS)
		return NULL;
	iterator->datum_index = iterator->next;
	return &players[iterator->next++];
}

static struct player_datum *player_try_and_get(long index)
{
	return index >= 0 && index < PLAYERS ? &players[index] : NULL;
}

static struct object_datum *object_try_and_get(long index)
{
	return index >= 0 && index < PLAYERS && objects[index].exists ? &objects[index] : NULL;
}

static struct object_datum *object_get(long index)
{
	return &objects[index];
}

static char player_name_character_ascii(short character)
{
	return (char)character;
}

static boolean game_engine_has_teams(void)
{
	return TRUE;
}

static long game_time_get(void)
{
	return 120;
}

static int bots_player_is_bot(long player_index)
{
	return player_index >= 0;
}

static long bots_player_of_bot(long bot_index)
{
	return bot_index < PLAYERS ? bot_index : NONE;
}

static void bots_command_json_ammo(struct server_json *out, long unit_index)
{
	(void)out;
	(void)unit_index;
}

static short bots_knowledge_item_count(void)
{
	return 0;
}

static boolean bots_knowledge_item(long item_index, struct bots_item *item, long *object_index)
{
	(void)item_index; (void)item; (void)object_index;
	return FALSE;
}

static boolean bots_knowledge_team_item(short team, long item_index, long *seen_tick, boolean *present,
	long *ticks_until)
{
	(void)team; (void)item_index; (void)seen_tick; (void)present; (void)ticks_until;
	return FALSE;
}

#include "under_test.inc"

static void player(long index, char const *name, long team, real x, real y, real z)
{
	long character;

	memset(&players[index], 0, sizeof(players[index]));
	for (character = 0; name[character] && character < 11; character++)
		players[index].name[character] = name[character];
	players[index].team_index = team;
	players[index].unit_index = index;
	objects[index].exists = TRUE;
	objects[index].object.parent_object_index = NONE;
	objects[index].object.position.x = x;
	objects[index].object.position.y = y;
	objects[index].object.position.z = z;
	objects[index].object.body_vitality = 1.0f;
}

static void world(void)
{
	memset(&bots_knowledge, 0, sizeof(bots_knowledge));
	/* (red: Alpha, Bravo; blue: Charlie, Delta) */
	player(0, "Alpha", 0, 1.0f, 1.0f, 1.0f);
	player(1, "Bravo", 0, 2.0f, 2.0f, 2.0f);
	player(2, "Charlie", 1, 50.0f, 50.0f, 5.0f);
	player(3, "Delta", 1, 60.0f, 60.0f, 6.0f);
	/* (red's bots saw Charlie at 40,40,4 on tick 90; blue's saw Alpha) */
	bots_knowledge.team_seen[0][0].latest.player_index = 2;
	bots_knowledge.team_seen[0][0].latest.tick = 90;
	bots_knowledge.team_seen[0][0].latest.position.x = 40.0f;
	bots_knowledge.team_seen[0][0].latest.position.y = 40.0f;
	bots_knowledge.team_seen[0][0].latest.position.z = 4.0f;
	bots_knowledge.team_seen[1][0].latest.player_index = 0;
	bots_knowledge.team_seen[1][0].latest.tick = 100;
	bots_knowledge.team_seen[1][0].latest.position.x = 3.0f;
}

static char const *team_view(short team)
{
	static char text[8192];
	struct server_json out;

	server_json_begin(&out);
	bots_command_team_json(&out, team);
	snprintf(text, sizeof(text), "%s", server_json_text(&out));
	server_json_free(&out);
	return text;
}

static void case_team_knows(void)
{
	char const *text;

	world();
	text = team_view(0);
	CHECK(strstr(text, "\"Alpha\"") && strstr(text, "\"Bravo\""), "red's own players: %s", text);
	CHECK(strstr(text, "[1,1,1]") && strstr(text, "[2,2,2]"), "where red's players are: %s", text);
	CHECK(strstr(text, "\"Charlie\"") && strstr(text, "[40,40,4]"), "Charlie where red saw him: %s", text);
	CHECK(strstr(text, "\"tick\":90"), "and when: %s", text);
	CHECK(!strstr(text, "[50,50,5]"), "never where Charlie is now: %s", text);
	CHECK(!strstr(text, "Delta") && !strstr(text, "[60,60,6]"), "never Delta, whom red did not see: %s", text);
	text = team_view(1);
	CHECK(strstr(text, "\"Alpha\"") && !strstr(text, "\"Bravo\"") && !strstr(text, "[1,1,1]"),
		"blue's: Alpha where blue saw him alone: %s", text);
}

static void event(char const *whole, unsigned long teams, char const *public_text)
{
	struct server_json json;

	server_json_begin(&json);
	server_json_raw(&json, NULL, whole);
	bots_command_event_add(&json, teams, public_text);
}

static char const *events(short team)
{
	static char text[16384];
	struct bots_command_reply reply;

	text[0] = 0;
	reply.text = text;
	reply.size = sizeof(text);
	reply.length = 0;
	bots_command_events_answer(&reply, 0, TRUE, team);
	return text;
}

static void case_events(void)
{
	char const *text;

	memset(&bots_command, 0, sizeof(bots_command));
	event("{\"event\":\"order\",\"bot\":\"Charlie\",\"target\":\"red flag\"}", 2, NULL);
	event("{\"event\":\"kill\",\"killer\":\"Charlie\",\"victim\":\"Delta\",\"position\":[60,60,6]}", 2,
		"{\"event\":\"kill\",\"killer\":\"Charlie\",\"victim\":\"Delta\"}");
	event("{\"event\":\"kill\",\"killer\":\"Alpha\",\"victim\":\"Charlie\",\"position\":[30,30,3]}", 3, NULL);
	event("{\"event\":\"flag_taken\",\"flag\":\"blue\"}", 3, NULL);
	text = events(0);
	CHECK(!strstr(text, "\"order\""), "red sees none of blue's orders: %s", text);
	CHECK(strstr(text, "\"victim\":\"Delta\"") && !strstr(text, "[60,60,6]"), "blue's kill, without where: %s", text);
	CHECK(strstr(text, "[30,30,3]"), "a kill red was part of, whole: %s", text);
	CHECK(strstr(text, "flag_taken"), "what everyone sees: %s", text);
	text = events(NONE);
	CHECK(strstr(text, "\"order\"") && strstr(text, "[60,60,6]"), "the host sees everything: %s", text);
}

int main(int argc, char **argv)
{
	static struct
	{
		char const *name;
		void (*run)(void);
	} const cases[] =
	{
		{ "team-knows", case_team_knows },
		{ "events", case_events },
	};
	long index;

	for (index = 0; argc > 1 && index < NUMBEROF(cases); index++)
	{
		if (!strcmp(argv[1], cases[index].name))
		{
			cases[index].run();
			return 0;
		}
	}
	fprintf(stderr, "no case %s\n", argc > 1 ? argv[1] : "");
	return 2;
}
