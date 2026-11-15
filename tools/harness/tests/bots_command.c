/*
BOTS_COMMAND.C (test)

The host's commands to its bots (port/linux/game/bots_command.c): a line
parsed as the game takes it (its orders, traits, counts and errors); the
traits an order of style or value sets; the goal each order gives a bot
(bots_command_objective: to a point pressing on, about it once there, a
hold, a defense, a teammate followed, the enemy flag's carrier hunted) and
that an attack on an enemy goes only where the bot's team saw them, never
where they are; the lines queued between frames applied at the tick in the
order they came, none before it; and the traits' effect on a bot's play
(bots.c's bots_bot_recharge_shield, bots_bot_team_minded).
test_bots_command.py takes the constants, structures and tables
(config.inc) and the code (under_test.inc).
*/

#include "harness.h"

#include <ctype.h>
#include <math.h>
#include <strings.h>

#define csmemset memset
#define csstrcasecmp strcasecmp
#define NUMBEROF(array) ((long)(sizeof(array) / sizeof((array)[0])))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define PIN(value, low, high) MIN(MAX(value, low), high)
#define VALID_INDEX(index, count) ((index) >= 0 && (index) < (count))
#define TICKS_PER_SECOND 30
#define BOTS_MAXIMUM 16

/* the game's objects and players, as the code reads them */
enum { _object_type_biped = 0, _object_type_weapon = 2 };
#define _object_mask_unit (1 << _object_type_biped)
#define _object_mask_weapon (1 << _object_type_weapon)
#define _object_dead_bit 0

struct object_datum
{
	struct
	{
		real_point3d position;
		long parent_object_index;
		short type;
		unsigned long damage_flags;
	} object;
	boolean exists;
	boolean flag;
	long player_index;
};

struct unit_datum
{
	struct
	{
		long player_index;
	} unit;
};

struct player_datum
{
	boolean quit_out_of_game;
	long unit_index;
	long team_index;
};

static struct object_datum objects[16];
static struct unit_datum units[16];
static struct player_datum players[16];
static boolean teams = TRUE;
static long carrying_player = NONE;

static struct object_datum *object_try_and_get(long index)
{
	return index >= 0 && index < 16 && objects[index].exists ? &objects[index] : NULL;
}

static struct unit_datum *unit_get(long index)
{
	units[index].unit.player_index = objects[index].player_index;
	return &units[index];
}

static struct player_datum *player_try_and_get(long index)
{
	return index >= 0 && index < 16 ? &players[index] : NULL;
}

static struct player_datum *player_get(long index)
{
	return &players[index];
}

static boolean weapon_is_flag(long index)
{
	return objects[index].flag;
}

static boolean game_engine_has_teams(void)
{
	return teams;
}

static boolean bots_objectives_carrying(long player_index)
{
	return player_index == carrying_player;
}

#include "config.inc"

/* what the bot's team knows of its enemies (bots_knowledge.c) */
static struct bots_known_enemy known_enemies[4];
static short known_count;

static short bots_knowledge_known(long bot_index, struct bots_known_enemy *known, short maximum)
{
	short index;

	(void)bot_index;
	for (index = 0; index < known_count && index < maximum; index++)
		known[index] = known_enemies[index];
	return index;
}

/* the events the code gives, by name */
static char last_event[32];
static int event_count;

static void bots_command_order_event(long bot_index, char const *name, char const *reason)
{
	(void)bot_index;
	(void)reason;
	snprintf(last_event, sizeof(last_event), "%s", name);
	event_count++;
}

/* the lines the tick applied, in order */
static char applied[8][256];
static int applied_count;

static void bots_command_apply(char const *line)
{
	if (applied_count < 8)
		snprintf(applied[applied_count], sizeof(applied[0]), "%s", line);
	applied_count++;
}

static void bots_command_traits_ready(void)
{
}

static void bots_command_flag_events(void)
{
}

/* bots.c's traits read by its own code */
struct bots_bot
{
	long index;
};

static real aggression_of = 0.5f;
static real teamwork_of = 1.0f;

static real bots_command_aggression(long bot_index)
{
	(void)bot_index;
	return aggression_of;
}

static real bots_command_teamwork(long bot_index)
{
	(void)bot_index;
	return teamwork_of;
}

#include "under_test.inc"

static void reset_world(void)
{
	memset(objects, 0, sizeof(objects));
	memset(players, 0, sizeof(players));
	memset(&bots_command, 0, sizeof(bots_command));
	carrying_player = NONE;
	known_count = 0;
	teams = TRUE;
	event_count = 0;
	last_event[0] = 0;
	/* (the bot: player 0 of red, its unit object 0; player 1 a teammate,
	unit 1; player 2 an enemy, unit 2) */
	players[0].unit_index = 0;
	players[0].team_index = 0;
	players[1].unit_index = 1;
	players[1].team_index = 0;
	players[2].unit_index = 2;
	players[2].team_index = 1;
	objects[0].exists = objects[1].exists = objects[2].exists = TRUE;
	objects[0].object.parent_object_index = objects[1].object.parent_object_index =
		objects[2].object.parent_object_index = NONE;
	objects[0].player_index = 0;
	objects[1].player_index = 1;
	objects[2].player_index = 2;
	objects[1].object.position.x = 20.0f;
	objects[2].object.position.x = 50.0f;
}

static real_point3d point_at(real x, real y)
{
	real_point3d point = { x, y, 0.0f };

	return point;
}

static void give(short kind, short target_kind, real_point3d point, long index)
{
	struct bots_command_order *order = &bots_command.orders[0];

	memset(order, 0, sizeof(*order));
	order->kind = kind;
	order->state = _bots_order_state_active;
	order->target.kind = target_kind;
	order->target.point = point;
	order->target.object_index = target_kind == _bots_target_object ? index : NONE;
	order->target.player_index = target_kind == _bots_target_player ? index : NONE;
}

static void case_parse(void)
{
	struct bots_command_request request;
	char error[200];

	CHECK(bots_command_parse("order red defend red flag", &request, error, sizeof(error)), "%s", error);
	CHECK(request.kind == _bots_request_order && request.order == _bots_command_defend, "defend");
	CHECK(!strcmp(request.who, "red") && !strcmp(request.target, "red flag"), "%s / %s", request.who, request.target);
	CHECK(bots_command_parse("order all red attack the blue base --json", &request, error, sizeof(error)), "%s", error);
	CHECK(!strcmp(request.who, "red") && request.order == _bots_command_attack && request.json, "all red: %s",
		request.who);
	CHECK(!strcmp(request.target, "blue base"), "'the' dropped: %s", request.target);
	CHECK(bots_command_parse("order Johnson,Jenkins go to \"warthog 1\"", &request, error, sizeof(error)), "%s", error);
	CHECK(request.order == _bots_command_goto && !strcmp(request.target, "warthog 1"), "goto %s", request.target);
	CHECK(bots_command_parse("order blue clear", &request, error, sizeof(error)) && request.order == _bots_command_free,
		"clear");
	CHECK(bots_command_parse("set blue aggression 0.9", &request, error, sizeof(error)), "%s", error);
	CHECK(request.kind == _bots_request_set && request.trait == _bots_trait_aggression &&
		!strcmp(request.value, "0.9"), "set");
	CHECK(bots_command_parse("set all accuracy legendary", &request, error, sizeof(error)) &&
		request.trait == _bots_trait_skill, "accuracy is skill");
	CHECK(bots_command_parse("add 2 blue", &request, error, sizeof(error)), "%s", error);
	CHECK(request.kind == _bots_request_add && request.count == 2 && request.team == 1, "add");
	CHECK(bots_command_parse("add", &request, error, sizeof(error)) && request.count == 1 && request.team == NONE,
		"add one");
	CHECK(bots_command_parse("fill 8", &request, error, sizeof(error)) && request.count == 8, "fill");
	CHECK(bots_command_parse("events 12", &request, error, sizeof(error)) && request.after == 12, "events");
	CHECK(bots_command_parse("team Johnson blue", &request, error, sizeof(error)) && request.team == 1, "team");
	/* (and what it refuses, saying why) */
	CHECK(!bots_command_parse("order red dance", &request, error, sizeof(error)) && strstr(error, "not an order"),
		"dance: %s", error);
	CHECK(!bots_command_parse("set red aggression 2", &request, error, sizeof(error)) && strstr(error, "0 to 1"),
		"aggression 2: %s", error);
	CHECK(!bots_command_parse("set red skill godlike", &request, error, sizeof(error)), "godlike");
	CHECK(!bots_command_parse("set red vehicles boat", &request, error, sizeof(error)), "boat");
	CHECK(!bots_command_parse("fill 1", &request, error, sizeof(error)), "fill 1");
	CHECK(!bots_command_parse("fill", &request, error, sizeof(error)), "fill nothing");
	CHECK(!bots_command_parse("order red follow", &request, error, sizeof(error)) && strstr(error, "needs a target"),
		"follow nobody: %s", error);
	CHECK(!bots_command_parse("order red", &request, error, sizeof(error)), "no order");
	CHECK(!bots_command_parse("add 2 green", &request, error, sizeof(error)), "green");
	CHECK(!bots_command_parse("dance", &request, error, sizeof(error)) && strstr(error, "not a command"), "dance");
	CHECK(bots_command_parse("", &request, error, sizeof(error)) && request.kind == _bots_request_help, "empty: help");
}

static void case_coordinates(void)
{
	real_point3d point;
	char normal[64];

	CHECK(bots_command_coordinates("10,-20.5,3", &point) && point.x == 10.0f && point.y == -20.5f && point.z == 3.0f,
		"x,y,z");
	CHECK(bots_command_coordinates("1 2 3", &point), "x y z");
	CHECK(!bots_command_coordinates("1 2", &point), "two");
	CHECK(!bots_command_coordinates("1 2 3 4", &point), "four");
	CHECK(!bots_command_coordinates("red base", &point), "words");
	bots_command_normalize("  Red_Base ", normal, sizeof(normal));
	CHECK(!strcmp(normal, "red base"), "normal: '%s'", normal);
	bots_command_normalize("Sniper-Rifle  2", normal, sizeof(normal));
	CHECK(!strcmp(normal, "sniper rifle 2"), "normal: '%s'", normal);
}

static void case_traits(void)
{
	struct bots_command_traits traits;

	bots_command_traits_default(&traits);
	CHECK(traits.skill == NONE && traits.aggression == 0.5f && traits.teamwork == 1.0f, "defaults");
	bots_command_trait_set(&traits, _bots_trait_style, "rusher");
	CHECK(traits.aggression == 0.9f && traits.teamwork == 0.3f, "rusher %g %g", traits.aggression, traits.teamwork);
	bots_command_trait_set(&traits, _bots_trait_skill, "heroic");
	CHECK(traits.skill == 2, "heroic");
	bots_command_trait_set(&traits, _bots_trait_skill, "default");
	CHECK(traits.skill == NONE, "skill default");
	bots_command_trait_set(&traits, _bots_trait_vehicles, "ghost,banshee");
	CHECK(traits.vehicles == ((1UL << 2) | (1UL << 4)), "vehicles %lx", traits.vehicles);
	bots_command_trait_set(&traits, _bots_trait_vehicles, "none");
	CHECK(traits.vehicles == 0, "none");
	bots_command_trait_set(&traits, _bots_trait_weapon, "Sniper_Rifle");
	CHECK(!strcmp(traits.weapon, "sniper rifle"), "weapon '%s'", traits.weapon);
	bots_command_trait_set(&traits, _bots_trait_aggression, "0.25");
	CHECK(traits.aggression == 0.25f, "aggression");
	bots_command_trait_set(&traits, _bots_trait_reset, "");
	CHECK(traits.aggression == 0.5f && traits.vehicles == BOTS_COMMAND_ALL_VEHICLES && !traits.weapon[0], "reset");
}

static void case_attack_point(void)
{
	struct bots_objective objective;
	real_point3d far_point = point_at(30.0f, 0.0f), here = point_at(0.0f, 0.0f);

	reset_world();
	give(_bots_command_attack, _bots_target_point, far_point, NONE);
	CHECK(bots_command_objective(0, 0, &here, &objective), "an objective");
	CHECK(objective.kind == _bots_objective_take && objective.press_on && objective.urgent &&
		objective.point.x == 30.0f && objective.object_index == NONE, "to the point, pressing on");
	CHECK(!event_count, "not there yet");
	here = point_at(28.0f, 1.0f);
	CHECK(bots_command_objective(0, 0, &here, &objective), "there");
	CHECK(!strcmp(last_event, "order_done") && bots_command.orders[0].state == _bots_order_state_done, "done");
	CHECK(objective.kind == _bots_objective_guard && objective.radius == BOTS_COMMAND_DEFEND_RADIUS && !objective.press_on,
		"holding the ground about it");
	CHECK(bots_command_objective(0, 0, &here, &objective) && event_count == 1, "done once");
	/* (a goto keeps less ground about its point) */
	give(_bots_command_goto, _bots_target_point, far_point, NONE);
	here = point_at(0.0f, 0.0f);
	CHECK(bots_command_objective(0, 0, &here, &objective) && objective.kind == _bots_objective_take, "goto");
}

static void case_defend(void)
{
	struct bots_objective objective;
	real_point3d post = point_at(5.0f, 5.0f), here = point_at(40.0f, 0.0f);

	reset_world();
	give(_bots_command_defend, _bots_target_point, post, NONE);
	CHECK(bots_command_objective(0, 0, &here, &objective), "defend");
	CHECK(objective.kind == _bots_objective_guard && objective.urgent && !objective.press_on &&
		objective.radius == BOTS_COMMAND_DEFEND_RADIUS && objective.point.x == 5.0f, "a guard of its ground");
	give(_bots_command_hold, _bots_target_point, post, NONE);
	CHECK(bots_command_objective(0, 0, &here, &objective), "hold");
	CHECK(objective.kind == _bots_objective_guard && objective.press_on && objective.radius == BOTS_COMMAND_HOLD_RADIUS,
		"a hold: its spot, kept through fights");
	give(_bots_command_free, _bots_target_none, post, NONE);
	CHECK(!bots_command_objective(0, 0, &here, &objective), "free: its own play");
}

static void case_enemy_unseen(void)
{
	struct bots_objective objective;
	real_point3d here = point_at(0.0f, 0.0f);

	reset_world();
	give(_bots_command_attack, _bots_target_player, here, 2);
	/* (the enemy at 50,0, which no one of the team saw: its own search) */
	CHECK(!bots_command_objective(0, 0, &here, &objective), "an enemy never seen gives no goal (no wall hack)");
}

static void case_enemy_seen(void)
{
	struct bots_objective objective;
	real_point3d here = point_at(0.0f, 0.0f);

	reset_world();
	give(_bots_command_attack, _bots_target_player, here, 2);
	known_enemies[0].player_index = 2;
	known_enemies[0].position = point_at(35.0f, 7.0f);
	known_count = 1;
	CHECK(bots_command_objective(0, 0, &here, &objective), "a goal");
	CHECK(objective.kind == _bots_objective_hunt && objective.player_index == 2, "hunting");
	CHECK(objective.point.x == 35.0f && objective.point.y == 7.0f,
		"where it was seen (%g,%g), not where it is (50,0)", objective.point.x, objective.point.y);
}

static void case_follow(void)
{
	struct bots_objective objective;
	real_point3d here = point_at(0.0f, 0.0f);

	reset_world();
	give(_bots_command_follow, _bots_target_player, here, 1);
	CHECK(bots_command_objective(0, 0, &here, &objective), "a goal");
	CHECK(objective.kind == _bots_objective_escort && objective.player_index == 1 && objective.point.x == 20.0f &&
		objective.radius == BOTS_COMMAND_FOLLOW_RADIUS, "with the teammate where they are");
	players[1].quit_out_of_game = TRUE;
	CHECK(!bots_command_objective(0, 0, &here, &objective) && !strcmp(last_event, "order_failed") &&
		bots_command.orders[0].kind == _bots_command_free, "the teammate left: failed, cleared");
}

static void case_flag(void)
{
	struct bots_objective objective;
	real_point3d here = point_at(0.0f, 0.0f);

	reset_world();
	/* (the enemy flag, object 5, lying at 60,0) */
	objects[5].exists = TRUE;
	objects[5].flag = TRUE;
	objects[5].object.type = _object_type_weapon;
	objects[5].object.parent_object_index = NONE;
	objects[5].object.position = point_at(60.0f, 0.0f);
	give(_bots_command_attack, _bots_target_object, point_at(60.0f, 0.0f), 5);
	CHECK(bots_command_objective(0, 0, &here, &objective), "a goal");
	CHECK(objective.kind == _bots_objective_take && objective.object_index == 5, "to take the flag");
	/* (an enemy carries it: hunted where the HUD shows) */
	objects[5].object.parent_object_index = 2;
	CHECK(bots_command_objective(0, 0, &here, &objective) && objective.kind == _bots_objective_hunt &&
		objective.player_index == 2, "its carrier hunted");
	/* (a teammate carries it: escorted) */
	objects[5].object.parent_object_index = 1;
	CHECK(bots_command_objective(0, 0, &here, &objective) && objective.kind == _bots_objective_escort, "escorted");
	/* (the bot has it: the game's own play, home) */
	carrying_player = 0;
	CHECK(!bots_command_objective(0, 0, &here, &objective) && !strcmp(last_event, "order_done"), "taken: done");
	carrying_player = NONE;
	objects[5].exists = FALSE;
	give(_bots_command_attack, _bots_target_object, point_at(60.0f, 0.0f), 5);
	CHECK(!bots_command_objective(0, 0, &here, &objective) && !strcmp(last_event, "order_failed"), "gone: failed");
}

static void case_queue(void)
{
	long index;

	reset_world();
	applied_count = 0;
	CHECK(bots_command_enqueue("order red defend red flag"), "one");
	CHECK(bots_command_enqueue("order red attack blue base"), "two");
	CHECK(bots_command_enqueue("set red aggression 0.9"), "three");
	CHECK(applied_count == 0, "nothing applied between ticks");
	bots_command_tick_begin();
	CHECK(applied_count == 3, "the tick applies them all (%d)", applied_count);
	CHECK(!strcmp(applied[0], "order red defend red flag") && !strcmp(applied[1], "order red attack blue base") &&
		!strcmp(applied[2], "set red aggression 0.9"), "in the order they came: %s | %s | %s", applied[0], applied[1],
		applied[2]);
	bots_command_tick_begin();
	CHECK(applied_count == 3, "once each");
	for (index = 0; index < BOTS_COMMAND_QUEUE; index++)
		CHECK(bots_command_enqueue("order red hold"), "room for %ld", index);
	CHECK(!bots_command_enqueue("order red hold"), "a full queue refuses");
}

static void case_aggression(void)
{
	struct bots_bot bot = { 3 };

	aggression_of = 0.5f;
	CHECK(bots_bot_recharge_shield(&bot) == 0.15f, "0.5 plays as the bots always did");
	aggression_of = 0.9f;
	CHECK(bots_bot_recharge_shield(&bot) < 0.0f, "an aggressive bot never falls back to recharge");
	aggression_of = 0.0f;
	CHECK(bots_bot_recharge_shield(&bot) > 0.3f, "a cautious one falls back sooner");
}

static void case_teamwork(void)
{
	struct bots_bot bot = { 3 };
	long now, minded = 0;

	teamwork_of = 1.0f;
	for (now = 0; now < 3000 * TICKS_PER_SECOND; now += TICKS_PER_SECOND * 3)
		minded += bots_bot_team_minded(&bot, now);
	CHECK(minded == 1000, "teamwork 1: always (%ld)", minded);
	teamwork_of = 0.0f;
	for (now = 0, minded = 0; now < 3000 * TICKS_PER_SECOND; now += TICKS_PER_SECOND * 3)
		minded += bots_bot_team_minded(&bot, now);
	CHECK(minded == 0, "teamwork 0: never (%ld)", minded);
	teamwork_of = 0.5f;
	for (now = 0, minded = 0; now < 3000 * TICKS_PER_SECOND; now += TICKS_PER_SECOND * 3)
		minded += bots_bot_team_minded(&bot, now);
	CHECK(minded > 400 && minded < 600, "teamwork 0.5: about half the turns (%ld)", minded);
}

int main(int argc, char **argv)
{
	static struct
	{
		char const *name;
		void (*run)(void);
	} const cases[] =
	{
		{ "parse", case_parse },
		{ "coordinates", case_coordinates },
		{ "traits", case_traits },
		{ "attack-point", case_attack_point },
		{ "defend", case_defend },
		{ "enemy-unseen", case_enemy_unseen },
		{ "enemy-seen", case_enemy_seen },
		{ "follow", case_follow },
		{ "flag", case_flag },
		{ "queue", case_queue },
		{ "aggression", case_aggression },
		{ "teamwork", case_teamwork },
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
