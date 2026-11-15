/*
BOTS_KNOWLEDGE.C

What a bot knows (bots_knowledge.h): the enemies it saw and its team saw,
and the map's item spawns as it saw them and as the game's clock turns
them.

bots.c tells what its bots see (bots_knowledge_saw, bots_knowledge_saw_item):
an enemy its look found, and each enemy and item spawn in its view that its
sight test reaches, a look every third of a second. A bot remembers the last
sighting of each enemy.

A team's bots share what they see (bots.share_sightings, on by default) as
a callout over the voice chat would: the team keeps the last sighting of
each enemy, and a report of it goes to its other bots, which have it
bots.share_delay after the sighting. While one report is on its way the
next waits, then goes with the last sighting there is, so an enemy kept in
sight is known to the team a delay late, a delay apart. What a bot knows is
its own sightings and the reports that reached it (bots_knowledge_known):
it goes and looks there; its aim is its own sight's alone, never through a
wall. With sharing off a bot knows only what it saw itself.

The items: the map's netgame equipment of this game's type, each with its
period as game_engine_update_item_spawn has it (an item comes back at every
multiple of its period on the game's clock, which a player can time). A bot
believes an item lies at its spawn as it last saw the spawn, there or not,
and that one gone is back at the spawn's next turn.

(The sightings' code came from the ai-bots branch's bots_external.c, whose
orders from outside are not here.)
*/

#include "cseries.h"
#include "math/real_math.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "items/items.h"
#include "items/item_definitions.h"
#include "items/weapons.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"

#include "bots.h"
#include "bots_knowledge.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* port/linux/src/port_config.c */
int config_boolean(char const *name);
double config_real(char const *name);
unsigned long config_changes(void);
void platform_log(char const *format, ...);

/* ---------- constants */

enum
{
	/* the sightings a bot remembers, and a team */
	BOTS_KNOWLEDGE_BOT_MEMORY = 16,
	BOTS_KNOWLEDGE_TEAM_MEMORY = 32,
	/* bots.share_delay's most (ticks) */
	BOTS_KNOWLEDGE_SHARE_DELAY_MAXIMUM_TICKS = 5 * TICKS_PER_SECOND,
};

/* ---------- structures */

/* where an enemy player was seen */
struct bots_knowledge_sighting
{
	long player_index;
	/* its unit then (a sighting of a life before tells nothing of this
	one) */
	long unit_index;
	/* when (0: none), and the bot that saw it (its player's datum) */
	long tick;
	long seen_by;
	real_point3d position;
	/* the way it faced (yaw) */
	real heading;
};

/* an enemy as a team knows it: its last sighting; the report of one on its
way to the team's other bots (tick 0: none) and when it was sent, and the
one that reached them (tick 0: none) */
struct bots_knowledge_team_sighting
{
	struct bots_knowledge_sighting latest;
	struct bots_knowledge_sighting sent;
	long sent_tick;
	struct bots_knowledge_sighting heard;
	/* when the report heard reached the team (0: none) */
	long heard_tick;
};

/* what a bot knows */
struct bots_knowledge_slot
{
	/* the bot's player (its datum), NONE for none */
	long player_index;
	struct bots_knowledge_sighting seen[BOTS_KNOWLEDGE_BOT_MEMORY];
	/* what it saw at each item's spawn, and when (0: never) */
	long item_seen_ticks[BOTS_KNOWLEDGE_MAXIMUM_ITEMS];
	boolean item_seen_present[BOTS_KNOWLEDGE_MAXIMUM_ITEMS];
};

/* an item spawn of the map (its netgame equipment, of this game's type) */
struct bots_knowledge_spawn
{
	short kind;
	boolean power;
	char name[BOTS_KNOWLEDGE_NAME_SIZE];
	real_point3d position;
	long period_ticks;
	/* its item collection: what may lie there */
	long collection_index;
	/* the item there now (NONE: none) */
	long object_index;
};

/* ---------- globals */

static struct
{
	struct bots_knowledge_slot slots[BOTS_MAXIMUM];
	/* each team's sightings, which reach its bots bots.share_delay late */
	struct bots_knowledge_team_sighting team_seen[2][BOTS_KNOWLEDGE_TEAM_MEMORY];
	/* bots.share_sightings and bots.share_delay (ticks), read again as the
	settings change (config_changes was settings_read_at) */
	boolean share;
	long share_delay_ticks;
	boolean settings_read;
	unsigned long settings_read_at;
	/* the item spawns, found once a map */
	boolean spawns_ready;
	short spawn_count;
	struct bots_knowledge_spawn spawns[BOTS_KNOWLEDGE_MAXIMUM_ITEMS];
	long items_tick;
	/* the minute line's: sightings, and reports that reached a team */
	long sightings;
	long reports;
} bots_knowledge;

/* ---------- small helpers */

/* a tag's name's last part ("weapons\rocket launcher\rocket launcher":
"rocket launcher") */
static void bots_knowledge_tag_short_name(long tag_index, char *name, long size)
{
	char const *full = tag_index != NONE ? tag_get_name(tag_index) : NULL;
	char const *last;

	if (size <= 0)
		return;
	name[0] = 0;
	if (!full)
		return;
	last = strrchr(full, '\\');
	last = last ? last + 1 : full;
	snprintf(name, (size_t)size, "%s", last);
}

/* the player's team as a bot's team knows it: 0 or 1 in a team game, NONE
without teams */
static short bots_knowledge_team(struct player_datum const *player)
{
	if (!game_engine_has_teams() || player->team_index < 0 || player->team_index > 1)
		return NONE;
	return (short)player->team_index;
}

/* ---------- the items */

static short bots_knowledge_item_kind(long definition_index, boolean *power)
{
	char const *name = definition_index != NONE ? tag_get_name(definition_index) : NULL;
	struct object_definition *definition = definition_index != NONE ? object_definition_get(definition_index) : NULL;

	*power = FALSE;
	if (!name || !definition)
		return _bots_item_weapon;
	if (definition->object.type == _object_type_weapon)
	{
		*power = strstr(name, "rocket") || strstr(name, "sniper") || strstr(name, "fuel rod") ||
			strstr(name, "flamethrower") || strstr(name, "shotgun") || strstr(name, "plasma_cannon");
		return _bots_item_weapon;
	}
	if (definition->object.type == _object_type_vehicle)
		return _bots_item_vehicle;
	if (strstr(name, "health"))
		return _bots_item_health;
	if (strstr(name, "grenade"))
		return _bots_item_grenades;
	*power = TRUE;
	return _bots_item_powerup;
}

/* an item collection's first item, NONE for none */
static long bots_knowledge_collection_item(long collection_index, long *permutation_count)
{
	struct item_collection_definition *collection;
	struct item_permutation_definition *permutation;

	*permutation_count = 0;
	if (collection_index == NONE)
		return NONE;
	collection = (struct item_collection_definition *)tag_get(ITEM_COLLECTION_DEFINITION_TAG, collection_index);
	if (!collection || collection->permutations.count <= 0)
		return NONE;
	*permutation_count = collection->permutations.count;
	permutation = TAG_BLOCK_GET_ELEMENT(&collection->permutations, 0, struct item_permutation_definition);
	return permutation->item.index;
}

/* whether an item of that definition is one of the collection's */
static boolean bots_knowledge_collection_has(long collection_index, long definition_index)
{
	struct item_collection_definition *collection;
	long index;

	if (collection_index == NONE || definition_index == NONE)
		return FALSE;
	collection = (struct item_collection_definition *)tag_get(ITEM_COLLECTION_DEFINITION_TAG, collection_index);
	if (!collection)
		return FALSE;
	for (index = 0; index < collection->permutations.count; index++)
	{
		struct item_permutation_definition *permutation =
			TAG_BLOCK_GET_ELEMENT(&collection->permutations, index, struct item_permutation_definition);

		if (permutation->item.index == definition_index)
			return TRUE;
	}
	return FALSE;
}

/* the map's item spawns of this game's type, in the scenario's order (the
first BOTS_KNOWLEDGE_MAXIMUM_ITEMS), with each's respawn period as
game_engine_update_item_spawn has it: an item comes at every multiple of
its period on the game's clock */
static void bots_knowledge_prepare_spawns(void)
{
	struct scenario *scenario = global_scenario_get();
	long index;

	bots_knowledge.spawns_ready = TRUE;
	bots_knowledge.spawn_count = 0;
	if (!scenario || !game_engine)
		return;
	for (index = 0; index < scenario->netgame_equipment.count && bots_knowledge.spawn_count < BOTS_KNOWLEDGE_MAXIMUM_ITEMS;
		index++)
	{
		struct scenario_netgame_equipment *equipment =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_equipment, index, struct scenario_netgame_equipment);
		struct bots_knowledge_spawn *spawn = &bots_knowledge.spawns[bots_knowledge.spawn_count];
		long period = 30 * TICKS_PER_SECOND, permutation_count, item_definition;

		if (!match_game_type(game_engine->type, NUMBEROF(equipment->game_type), equipment->game_type) ||
			equipment->item_collection.index == NONE)
		{
			continue;
		}
		if (equipment->spawn_time != 0)
			period = equipment->spawn_time * TICKS_PER_SECOND;
		else
		{
			struct item_collection_definition *collection = (struct item_collection_definition *)tag_get(
				ITEM_COLLECTION_DEFINITION_TAG, equipment->item_collection.index);

			if (collection && collection->spawn_time != 0)
				period = collection->spawn_time * TICKS_PER_SECOND;
		}
		item_definition = bots_knowledge_collection_item(equipment->item_collection.index, &permutation_count);
		if (item_definition == NONE)
			continue;
		csmemset(spawn, 0, sizeof(*spawn));
		spawn->kind = bots_knowledge_item_kind(item_definition, &spawn->power);
		/* (one item: its name; a random one: the collection's) */
		bots_knowledge_tag_short_name(permutation_count == 1 ? item_definition : equipment->item_collection.index,
			spawn->name, sizeof(spawn->name));
		spawn->position = equipment->position;
		spawn->period_ticks = period > 0 ? period : 30 * TICKS_PER_SECOND;
		spawn->collection_index = equipment->item_collection.index;
		spawn->object_index = NONE;
		bots_knowledge.spawn_count++;
	}
	platform_log("bots: %d item spawns on this map", (int)bots_knowledge.spawn_count);
}

/* the items at their spawns now: an item of the spawn's collection lying
(no one's) within half a unit of it is its (another, a weapon a player
dropped there, is not) */
static void bots_knowledge_update_items(void)
{
	struct object_iterator iterator;
	struct object_datum *object;
	short index;

	if (!bots_knowledge.spawns_ready)
		bots_knowledge_prepare_spawns();
	if (bots_knowledge.items_tick == game_time_get())
		return;
	bots_knowledge.items_tick = game_time_get();
	for (index = 0; index < bots_knowledge.spawn_count; index++)
		bots_knowledge.spawns[index].object_index = NONE;
	if (!bots_knowledge.spawn_count)
		return;
	object_iterator_new(&iterator, _object_mask_weapon | _object_mask_equipment, 0);
	while ((object = (struct object_datum *)object_iterator_next(&iterator)) != NULL)
	{
		if (object->object.parent_object_index != NONE || !TEST_FLAG(object->object.flags, _object_connected_to_map_bit))
			continue;
		for (index = 0; index < bots_knowledge.spawn_count; index++)
		{
			struct bots_knowledge_spawn *spawn = &bots_knowledge.spawns[index];
			real dx = object->object.position.x - spawn->position.x;
			real dy = object->object.position.y - spawn->position.y;
			real dz = object->object.position.z - spawn->position.z;

			if (spawn->object_index == NONE && dx * dx + dy * dy < 0.25f && dz > -0.6f && dz < 0.6f &&
				bots_knowledge_collection_has(spawn->collection_index, object->definition_index))
			{
				spawn->object_index = iterator.index;
				break;
			}
		}
	}
}

short bots_knowledge_item_count(void)
{
	if (!bots_knowledge.spawns_ready)
		bots_knowledge_prepare_spawns();
	return bots_knowledge.spawn_count;
}

boolean bots_knowledge_item(long item_index, struct bots_item *item, long *object_index)
{
	struct bots_knowledge_spawn *spawn;

	bots_knowledge_update_items();
	if (item_index < 0 || item_index >= bots_knowledge.spawn_count)
		return FALSE;
	spawn = &bots_knowledge.spawns[item_index];
	/* (the items are found once a tick, and one may be picked up later in
	that tick) */
	if (spawn->object_index != NONE)
	{
		struct object_datum *object = (struct object_datum *)object_try_and_get_and_verify_type(spawn->object_index,
			_object_mask_weapon | _object_mask_equipment);

		if (!object || object->object.parent_object_index != NONE)
			spawn->object_index = NONE;
	}
	if (item)
	{
		csmemset(item, 0, sizeof(*item));
		item->kind = spawn->kind;
		item->power = spawn->power;
		item->position = spawn->position;
		item->period_ticks = spawn->period_ticks;
		if (spawn->object_index != NONE)
		{
			char name[BOTS_KNOWLEDGE_NAME_SIZE];

			/* (what lies there: a random collection's choice) */
			bots_knowledge_tag_short_name(object_get(spawn->object_index)->definition_index, name, sizeof(name));
			snprintf(item->name, sizeof(item->name), "%s", name[0] ? name : spawn->name);
		}
		else
			snprintf(item->name, sizeof(item->name), "%s", spawn->name);
	}
	if (object_index)
		*object_index = spawn->object_index;
	return TRUE;
}

boolean bots_knowledge_item_believed(long bot_index, long item_index, long *ticks_until)
{
	struct bots_knowledge_slot const *slot;
	long now = game_time_get(), seen, period;

	if (ticks_until)
		*ticks_until = NONE;
	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM || item_index < 0 || item_index >= bots_knowledge.spawn_count)
		return FALSE;
	slot = &bots_knowledge.slots[bot_index];
	seen = slot->item_seen_ticks[item_index];
	period = bots_knowledge.spawns[item_index].period_ticks;
	/* (never seen: there, as the game started it; seen there: there) */
	if (seen <= 0 || slot->item_seen_present[item_index])
		return TRUE;
	/* (seen gone: back at the spawn's next turn after) */
	if (now / period > seen / period)
		return TRUE;
	if (ticks_until)
		*ticks_until = (seen / period + 1) * period - now;
	return FALSE;
}

/* ---------- what bots see */

/* a sighting of the player there now, by the bot (its player's datum) */
static void bots_knowledge_sighting_new(struct bots_knowledge_sighting *sighting, long player_index,
	real_point3d const *position, long seen_by)
{
	struct player_datum *player = player_try_and_get(player_index);

	csmemset(sighting, 0, sizeof(*sighting));
	sighting->player_index = player_index;
	sighting->unit_index = NONE;
	sighting->tick = game_time_get() > 0 ? game_time_get() : 1;
	sighting->seen_by = seen_by;
	sighting->position = *position;
	if (player && player->unit_index != NONE)
	{
		struct unit_datum *unit = unit_get(player->unit_index);

		sighting->unit_index = player->unit_index;
		sighting->heading = (real)atan2(unit->object.forward.j, unit->object.forward.i);
	}
}

/* a bot's memory: the player's sighting replaced, else the oldest */
static void bots_knowledge_remember(struct bots_knowledge_sighting *memory, long count,
	struct bots_knowledge_sighting const *sighting)
{
	long index, oldest = 0;

	for (index = 0; index < count; index++)
	{
		if (memory[index].player_index == sighting->player_index && memory[index].tick > 0)
		{
			oldest = index;
			break;
		}
		if (memory[index].tick < memory[oldest].tick)
			oldest = index;
	}
	memory[oldest] = *sighting;
}

/* a team's reports on their way: one that has been delay_ticks on its way
reaches the team's bots, and the next goes then with the last sighting
there is, if it is newer */
static void bots_knowledge_team_pass(struct bots_knowledge_team_sighting *entry, long now, long delay_ticks)
{
	while (entry->sent.tick > 0 && now - entry->sent_tick >= delay_ticks)
	{
		entry->heard = entry->sent;
		entry->heard_tick = now;
		entry->sent.tick = 0;
		if (entry->latest.tick > entry->heard.tick)
		{
			entry->sent = entry->latest;
			entry->sent_tick = now;
		}
	}
}

/* a team's sighting: the player's last, and a report of it sent unless one
is on its way (the player's entry, else the oldest's) */
static void bots_knowledge_team_note(struct bots_knowledge_team_sighting *memory, long count,
	struct bots_knowledge_sighting const *sighting, long now, long delay_ticks)
{
	struct bots_knowledge_team_sighting *entry;
	long index, oldest = 0;

	for (index = 0; index < count; index++)
	{
		if (memory[index].latest.player_index == sighting->player_index && memory[index].latest.tick > 0)
		{
			oldest = index;
			break;
		}
		if (memory[index].latest.tick < memory[oldest].latest.tick)
			oldest = index;
	}
	entry = &memory[oldest];
	if (entry->latest.player_index != sighting->player_index || entry->latest.tick <= 0)
		csmemset(entry, 0, sizeof(*entry));
	entry->latest = *sighting;
	if (entry->sent.tick <= 0)
	{
		entry->sent = *sighting;
		entry->sent_tick = now;
	}
	bots_knowledge_team_pass(entry, now, delay_ticks);
}

/* a sighting within BOTS_KNOWLEDGE_SEEN_MEMORY_TICKS */
static boolean bots_knowledge_fresh(struct bots_knowledge_sighting const *sighting, long now)
{
	return sighting->tick > 0 && now - sighting->tick <= BOTS_KNOWLEDGE_SEEN_MEMORY_TICKS;
}

/* the player of a sighting still of use to a team (NONE: without teams):
fresh, there, alive in the life seen, not of the team; NULL for none */
static struct player_datum *bots_knowledge_sighting_player(struct bots_knowledge_sighting const *sighting, short team,
	long now)
{
	struct player_datum *player;

	if (!bots_knowledge_fresh(sighting, now))
		return NULL;
	player = player_try_and_get(sighting->player_index);
	if (!player || player->quit_out_of_game || player->unit_index == NONE || player->unit_index != sighting->unit_index ||
		(team != NONE && bots_knowledge_team(player) == team))
	{
		return NULL;
	}
	return player;
}

void bots_knowledge_saw(long bot_index, long player_index, real_point3d const *position)
{
	struct bots_knowledge_slot *slot;
	struct bots_knowledge_sighting sighting;
	struct player_datum *bot_player;
	short team;

	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM)
		return;
	slot = &bots_knowledge.slots[bot_index];
	bot_player = slot->player_index != NONE ? player_try_and_get(slot->player_index) : NULL;
	if (!bot_player)
		return;
	bots_knowledge_sighting_new(&sighting, player_index, position, slot->player_index);
	bots_knowledge_remember(slot->seen, BOTS_KNOWLEDGE_BOT_MEMORY, &sighting);
	bots_knowledge.sightings++;
	team = bots_knowledge_team(bot_player);
	if (team != NONE)
	{
		bots_knowledge_team_note(bots_knowledge.team_seen[team], BOTS_KNOWLEDGE_TEAM_MEMORY, &sighting, game_time_get(),
			bots_knowledge.share_delay_ticks);
	}
}

void bots_knowledge_saw_item(long bot_index, long item_index, boolean present)
{
	struct bots_knowledge_slot *slot;

	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM || item_index < 0 || item_index >= BOTS_KNOWLEDGE_MAXIMUM_ITEMS)
		return;
	slot = &bots_knowledge.slots[bot_index];
	if (slot->player_index == NONE)
		return;
	slot->item_seen_ticks[item_index] = game_time_get() > 0 ? game_time_get() : 1;
	slot->item_seen_present[item_index] = present;
}

/* a sighting added to what a bot knows: the player's there replaced by a
later one; how many there are */
static short bots_knowledge_known_add(struct bots_known_enemy *known, short count, short maximum,
	struct bots_knowledge_sighting const *sighting, boolean own)
{
	short index;

	for (index = 0; index < count; index++)
	{
		if (known[index].player_index == sighting->player_index)
			break;
	}
	if (index < count && known[index].tick >= sighting->tick)
		return count;
	if (index >= count)
	{
		if (count >= maximum)
			return count;
		count++;
	}
	known[index].player_index = sighting->player_index;
	known[index].tick = sighting->tick;
	known[index].position = sighting->position;
	known[index].heading = sighting->heading;
	known[index].own = own;
	return count;
}

/* what a bot knows of the enemies (its slot, its team): its own sightings
and (bots.share_sightings) the reports that reached its team, never the
team's last sighting before its report came */
static short bots_knowledge_slot_known(struct bots_knowledge_slot const *slot, short team,
	struct bots_known_enemy *known, short maximum)
{
	long now = game_time_get(), index;
	short count = 0;

	for (index = 0; index < BOTS_KNOWLEDGE_BOT_MEMORY; index++)
	{
		if (bots_knowledge_sighting_player(&slot->seen[index], team, now))
			count = bots_knowledge_known_add(known, count, maximum, &slot->seen[index], TRUE);
	}
	if (bots_knowledge.share && team != NONE)
	{
		for (index = 0; index < BOTS_KNOWLEDGE_TEAM_MEMORY; index++)
		{
			struct bots_knowledge_sighting const *heard = &bots_knowledge.team_seen[team][index].heard;

			if (bots_knowledge_sighting_player(heard, team, now))
				count = bots_knowledge_known_add(known, count, maximum, heard, heard->seen_by == slot->player_index);
		}
	}
	return count;
}

short bots_knowledge_known(long bot_index, struct bots_known_enemy *known, short maximum)
{
	struct bots_knowledge_slot *slot;
	struct player_datum *bot_player;

	if (bot_index < 0 || bot_index >= BOTS_MAXIMUM || maximum <= 0)
		return 0;
	slot = &bots_knowledge.slots[bot_index];
	bot_player = slot->player_index != NONE ? player_try_and_get(slot->player_index) : NULL;
	if (!bot_player)
		return 0;
	return bots_knowledge_slot_known(slot, bots_knowledge_team(bot_player), known, maximum);
}

/* ---------- the tick */

/* bots.share_delay in ticks, as the bots use it: 0 to its most, none for
not a number */
static long bots_knowledge_share_delay_ticks(void)
{
	double delay = config_real("bots.share_delay");

	return delay == delay ?
		(long)(PIN(delay, 0.0, (double)BOTS_KNOWLEDGE_SHARE_DELAY_MAXIMUM_TICKS / TICKS_PER_SECOND) *
			TICKS_PER_SECOND + 0.5) : 0;
}

void bots_knowledge_tick_begin(void)
{
	struct data_iterator iterator;
	struct player_datum *player;
	long now = game_time_get(), team, index;

	/* (the settings read again only as they change: config_changes) */
	if (!bots_knowledge.settings_read || bots_knowledge.settings_read_at != config_changes())
	{
		bots_knowledge.settings_read_at = config_changes();
		bots_knowledge.settings_read = TRUE;
		bots_knowledge.share = config_boolean("bots.share_sightings") != 0;
		bots_knowledge.share_delay_ticks = bots_knowledge_share_delay_ticks();
	}
	for (team = 0; team < 2; team++)
	{
		for (index = 0; index < BOTS_KNOWLEDGE_TEAM_MEMORY; index++)
		{
			struct bots_knowledge_team_sighting *entry = &bots_knowledge.team_seen[team][index];
			long heard = entry->heard_tick;

			bots_knowledge_team_pass(entry, now, bots_knowledge.share_delay_ticks);
			if (entry->heard_tick != heard)
				bots_knowledge.reports++;
		}
	}
	bots_knowledge_update_items();
	/* (each bot's slot its player's: a new player's forgets the last's) */
	data_iterator_new(&iterator, player_data);
	while ((player = (struct player_datum *)data_iterator_next(&iterator)) != NULL)
	{
		long bot_index;
		struct bots_knowledge_slot *slot;

		if (player->local_player_index != NONE || player->quit_out_of_game ||
			!bots_machine_is_bot(player->network_player_data.machine_index))
		{
			continue;
		}
		bot_index = player->network_player_data.machine_index - BOTS_FIRST_MACHINE;
		slot = &bots_knowledge.slots[bot_index];
		if (slot->player_index != iterator.datum_index)
		{
			csmemset(slot, 0, sizeof(*slot));
			slot->player_index = iterator.datum_index;
		}
	}
}

void bots_knowledge_reset(void)
{
	long index;

	csmemset(&bots_knowledge, 0, sizeof(bots_knowledge));
	for (index = 0; index < BOTS_MAXIMUM; index++)
		bots_knowledge.slots[index].player_index = NONE;
	bots_knowledge.items_tick = NONE;
}

short bots_knowledge_team_known(short team, struct bots_known_enemy *known, short maximum)
{
	short count = 0;
	long index;

	if (team < 0 || team > 1)
		return 0;
	for (index = 0; index < BOTS_KNOWLEDGE_TEAM_MEMORY && count < maximum; index++)
	{
		struct bots_knowledge_sighting const *latest = &bots_knowledge.team_seen[team][index].latest;

		if (latest->tick <= 0 || latest->player_index == NONE)
			continue;
		known[count].player_index = latest->player_index;
		known[count].tick = latest->tick;
		known[count].position = latest->position;
		known[count].heading = latest->heading;
		known[count].own = TRUE;
		count++;
	}
	return count;
}

boolean bots_knowledge_team_item(short team, long item_index, long *seen_tick, boolean *present, long *ticks_until)
{
	long index, seen = 0, period, now = game_time_get();
	boolean there = FALSE;

	*seen_tick = 0;
	*present = FALSE;
	*ticks_until = NONE;
	if (team < 0 || team > 1 || item_index < 0 || item_index >= bots_knowledge.spawn_count)
		return FALSE;
	for (index = 0; index < BOTS_MAXIMUM; index++)
	{
		struct bots_knowledge_slot const *slot = &bots_knowledge.slots[index];
		struct player_datum *player = slot->player_index != NONE ? player_try_and_get(slot->player_index) : NULL;

		if (!player || bots_knowledge_team(player) != team || slot->item_seen_ticks[item_index] <= seen)
			continue;
		seen = slot->item_seen_ticks[item_index];
		there = slot->item_seen_present[item_index];
	}
	if (seen <= 0)
		return FALSE;
	*seen_tick = seen;
	*present = there;
	period = bots_knowledge.spawns[item_index].period_ticks;
	if (!there && period > 0 && now / period <= seen / period)
		*ticks_until = (seen / period + 1) * period - now;
	return TRUE;
}

void bots_knowledge_report(char *line, long size)
{
	if (size <= 0)
		return;
	snprintf(line, (size_t)size, ", sightings %ld (reports %ld%s)", bots_knowledge.sightings, bots_knowledge.reports,
		bots_knowledge.share ? "" : ", sharing off");
}
