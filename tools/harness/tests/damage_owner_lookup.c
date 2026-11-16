/* The player a damage is credited to, from its owner or the owner's parents
(source/objects/damage.c's get_player_index_from_object_or_parents): a
player's unit, or the first one up the parents; NONE for an AI's unit or
an object no player's unit carries. An owner or parent that is gone (deleted,
or its slot reused by another object) ends the walk with NONE: it is not
looked up as if it were there. The objects are made-up ones in slots with
identifiers, as the game's data arrays are; a strict lookup of a gone one
fails as the game's assertion would. */
#include "harness.h"

#define MAXIMUM_OBJECTS 8

struct object_datum
{
	struct
	{
		long parent_object_index;
	} object;
};

static struct
{
	short identifier; /* 0: free */
	boolean unit;
	long player_index;
	struct object_datum datum;
} slots[MAXIMUM_OBJECTS];
static short next_identifier = 0x100;

static struct object_datum *object_lookup(long object_index, boolean strict)
{
	short identifier = (short)(object_index >> 16);
	short absolute = (short)object_index;

	if (object_index != NONE && absolute >= 0 && absolute < MAXIMUM_OBJECTS && slots[absolute].identifier &&
		slots[absolute].identifier == identifier)
		return &slots[absolute].datum;
	CHECK(!strict, "a gone object %08lx looked up as if it were there", (unsigned long)object_index);
	return NULL;
}
#define object_try_and_get(index) object_lookup((index), FALSE)
#define object_get(index) object_lookup((index), TRUE)
#define unit_try_and_get(index) (object_lookup((index), FALSE) && slots[(short)(index)].unit ? \
	&slots[(short)(index)].datum : NULL)
#define player_index_from_unit_index(index) (slots[(short)(index)].player_index)

#include "under_test.inc"

static long made(short absolute, boolean unit, long player_index, long parent_index)
{
	slots[absolute].identifier = next_identifier++;
	slots[absolute].unit = unit;
	slots[absolute].player_index = player_index;
	slots[absolute].datum.object.parent_object_index = parent_index;
	return ((long)slots[absolute].identifier << 16) | absolute;
}

static void deleted(long object_index)
{
	slots[(short)object_index].identifier = 0;
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	long player = made(0, TRUE, 3, NONE);

	CASE("player_unit")
		CHECK(get_player_index_from_object_or_parents(player) == 3, "a player's unit not its player's");
	else CASE("parented")
	{
		long held = made(1, FALSE, NONE, player);

		CHECK(get_player_index_from_object_or_parents(held) == 3, "a player's weapon not its player's");
	}
	else CASE("several_parents")
	{
		long vehicle = made(1, FALSE, NONE, player), turret = made(2, FALSE, NONE, vehicle);

		CHECK(get_player_index_from_object_or_parents(turret) == 3, "two parents up not the player's");
	}
	else CASE("ai_unit")
	{
		long ai = made(1, TRUE, NONE, NONE), carried = made(2, FALSE, NONE, ai);

		CHECK(get_player_index_from_object_or_parents(ai) == NONE, "an AI's unit a player's");
		CHECK(get_player_index_from_object_or_parents(carried) == NONE, "an AI's weapon a player's");
	}
	else CASE("unparented")
		CHECK(get_player_index_from_object_or_parents(made(1, FALSE, NONE, NONE)) == NONE, "scenery a player's");
	else CASE("none")
		CHECK(get_player_index_from_object_or_parents(NONE) == NONE, "no owner a player's");
	else CASE("owner_gone")
	{
		/* a projectile's shooter, a non-unit owner, deleted */
		long owner = made(1, FALSE, NONE, player);

		deleted(owner);
		CHECK(get_player_index_from_object_or_parents(owner) == NONE, "a deleted owner a player's");
	}
	else CASE("parent_gone")
	{
		long vehicle = made(1, FALSE, NONE, NONE), turret = made(2, FALSE, NONE, vehicle);

		deleted(vehicle);
		CHECK(get_player_index_from_object_or_parents(turret) == NONE, "a deleted parent a player's");
	}
	else CASE("slot_reused")
	{
		/* the owner's slot now another player's weapon: not that player's */
		long owner = made(1, FALSE, NONE, NONE), other = made(2, TRUE, 5, NONE);

		deleted(owner);
		made(1, FALSE, NONE, other);
		CHECK(get_player_index_from_object_or_parents(owner) == NONE, "a reused slot's new object's player");
	}
	else
	{
		fprintf(stderr, "unknown case %s\n", case_name);
		return 2;
	}
	return 0;
}
