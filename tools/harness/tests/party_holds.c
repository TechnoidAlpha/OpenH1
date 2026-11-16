#include "harness.h"

/* Room held for a party joining a game (port/linux/game/network_party.c's
holds, the host's): a leader's ask held as far as the game has room (its
slots free of players and of other parties' holds: its people's, the bots
apart, which party_invites.c's holds case checks), the party's earlier
holds replaced, a
member held once; a hold lapsing when its member does not come, and let go
with its party; a slot held taken by its member alone; a party's players
put on its team where the team has room; and what the server browser says
of a game's room for the party (menu_functions.c's browser_party_room),
held or not (a Legacy host holds nothing: no room guarantee) */

#define csmemcmp memcmp
#define csmemcpy memcpy

#include "config.inc"
#include "under_test.inc"

static struct party_hold holds[MAXIMUM_PARTY_HOLDS];

/* a member's key hash: its number in each byte */
static byte hashes[64][PARTY_KEY_HASH_SIZE];

static void hashes_make(void)
{
	short index;

	for (index = 0; index < 64; index++)
		memset(hashes[index], index + 1, PARTY_KEY_HASH_SIZE);
}

/* members first to first + count - 1, as a leader names them */
static byte const *members(short first, short count, byte *list)
{
	short index;

	for (index = 0; index < count; index++)
		memcpy(list + index * PARTY_KEY_HASH_SIZE, hashes[first + index], PARTY_KEY_HASH_SIZE);
	return list;
}

static void reset(void)
{
	memset(holds, 0, sizeof(holds));
	hashes_make();
}

static void case_grant(void)
{
	byte list[64 * PARTY_KEY_HASH_SIZE];

	reset();
	/* (a game of 16 with 10 players: room for 6) */
	CHECK(party_holds_grant(holds, 3, members(0, 8, list), 8, 10, 16, 1000) == 6,
		"a party of 9 joining a game with room for 6: 6 held");
	CHECK(party_holds_count(holds) == 6, "6 held");
	CHECK(party_holds_find(holds, hashes[5]) != NONE && party_holds_find(holds, hashes[6]) == NONE,
		"the first 6 named held, the rest not");
	CHECK(party_holds_grant(holds, 4, members(20, 2, list), 2, 10, 16, 1000) == 0,
		"another party finds no room: the held slots are not its");
	CHECK(party_holds_grant(holds, 3, members(0, 3, list), 3, 10, 16, 2000) == 3,
		"the party asks again (its members changed): its earlier holds replaced");
	CHECK(party_holds_count(holds) == 3 && party_holds_find(holds, hashes[4]) == NONE,
		"those it no longer names let go");
	CHECK(holds[party_holds_find(holds, hashes[0])].time == 2000, "held anew from the second ask");
	CHECK(party_holds_grant(holds, 4, members(1, 2, list), 2, 10, 16, 2000) == 0,
		"members another party holds are not held again");
	memcpy(list, hashes[30], PARTY_KEY_HASH_SIZE);
	memcpy(list + PARTY_KEY_HASH_SIZE, hashes[30], PARTY_KEY_HASH_SIZE);
	CHECK(party_holds_grant(holds, 5, list, 2, 10, 16, 2000) == 1, "a member named twice is held once");
	reset();
	CHECK(party_holds_grant(holds, 3, members(0, 4, list), 4, 16, 16, 1000) == 0, "a full game holds nothing");
	CHECK(party_holds_grant(holds, 3, members(0, 0, list), 0, 4, 16, 1000) == 0, "a party of one asks for none");
}

static void case_expire(void)
{
	byte list[64 * PARTY_KEY_HASH_SIZE];

	reset();
	party_holds_grant(holds, 3, members(0, 3, list), 3, 4, 16, 1000);
	CHECK(party_holds_expire(holds, 1000 + PARTY_HOLD_MILLISECONDS - 1) == 0 && party_holds_count(holds) == 3,
		"kept while its members may still come");
	CHECK(party_holds_expire(holds, 1000 + PARTY_HOLD_MILLISECONDS) == 3 && party_holds_count(holds) == 0,
		"let go once they have not come in time");
	party_holds_grant(holds, 3, members(0, 3, list), 3, 4, 16, (unsigned long)0 - 100);
	CHECK(party_holds_expire(holds, 200) == 0, "kept across the clock's wrap");
	CHECK(party_holds_expire(holds, PARTY_HOLD_MILLISECONDS) == 3, "and let go after it");
}

static void case_release(void)
{
	byte list[64 * PARTY_KEY_HASH_SIZE];

	reset();
	party_holds_grant(holds, 3, members(0, 3, list), 3, 4, 16, 1000);
	party_holds_grant(holds, 7, members(10, 2, list), 2, 4, 16, 1000);
	CHECK(party_holds_release(holds, 3) == 3, "a leader gone: its party's holds let go");
	CHECK(party_holds_count(holds) == 2 && party_holds_find(holds, hashes[10]) != NONE,
		"another party's kept");
	CHECK(party_holds_release(holds, 3) == 0, "nothing more of it");
}

static void case_admit(void)
{
	byte list[64 * PARTY_KEY_HASH_SIZE];

	reset();
	CHECK(party_holds_admit(holds, hashes[40], 15, 16), "nothing held: anyone takes a free slot");
	CHECK(!party_holds_admit(holds, hashes[40], 16, 16), "none in a full game");
	party_holds_grant(holds, 3, members(0, 3, list), 3, 13, 16, 1000);
	CHECK(!party_holds_admit(holds, hashes[40], 13, 16), "the room left held: a stranger finds the game full");
	CHECK(!party_holds_admit(holds, NULL, 13, 16), "as does a machine of this network (no tunnel key)");
	CHECK(party_holds_admit(holds, hashes[1], 13, 16), "a member takes its slot");
	/* (its player added: its hold taken up) */
	holds[party_holds_find(holds, hashes[1])].used = FALSE;
	CHECK(party_holds_admit(holds, hashes[2], 14, 16), "the next member takes its own");
	CHECK(!party_holds_admit(holds, hashes[40], 14, 16), "a stranger still none");
	reset();
	party_holds_grant(holds, 3, members(0, 2, list), 2, 10, 16, 1000);
	CHECK(party_holds_admit(holds, hashes[40], 10, 16), "room beyond the held: a stranger takes it");
	CHECK(!party_holds_admit(holds, hashes[40], 14, 16), "but not the held");
	CHECK(!party_holds_admit(holds, hashes[0], 16, 16), "a member of a game filled meanwhile finds it full");
}

static void case_team(void)
{
	short party_counts[2], team_counts[2];

	party_counts[0] = 0, party_counts[1] = 2;
	team_counts[0] = 5, team_counts[1] = 3;
	CHECK(party_team_choice(party_counts, team_counts, 16) == 1, "its party's team, though the bigger one's not it");
	party_counts[0] = 0, party_counts[1] = 0;
	CHECK(party_team_choice(party_counts, team_counts, 16) == NONE, "no one of its party in yet: anyone's choice");
	party_counts[0] = 1, party_counts[1] = 2;
	CHECK(party_team_choice(party_counts, team_counts, 16) == 1, "a party split already: the team most are on");
	party_counts[0] = 0, party_counts[1] = 4;
	team_counts[0] = 2, team_counts[1] = 8;
	CHECK(party_team_choice(party_counts, team_counts, 16) == NONE, "its team full (half the game): not on it");
	team_counts[1] = 7;
	CHECK(party_team_choice(party_counts, team_counts, 16) == 1, "room for one more: on it");
	team_counts[1] = 8;
	CHECK(party_team_choice(party_counts, team_counts, 15) == NONE, "half of 15 is 8");
	team_counts[1] = 7;
	CHECK(party_team_choice(party_counts, team_counts, 15) == 1, "and 7 has room for one");
	/* Doubles' (4 at most): a party of two's second player on its leader's
	team, and no third */
	party_counts[0] = 1, party_counts[1] = 0;
	team_counts[0] = 1, team_counts[1] = 1;
	CHECK(party_team_choice(party_counts, team_counts, 4) == 0, "Doubles: its leader's team");
	team_counts[0] = 2;
	CHECK(party_team_choice(party_counts, team_counts, 4) == NONE, "Doubles: two on a team at most");
}

static void case_room(void)
{
	CHECK(browser_party_room(0, 4, 16, TRUE) == _browser_party_none, "in no party: nothing said");
	CHECK(browser_party_room(1, 4, 16, TRUE) == _browser_party_none, "a party of one: nothing said");
	CHECK(browser_party_room(4, 12, 16, TRUE) == _browser_party_held, "room for the four, held by its host");
	CHECK(browser_party_room(4, 13, 16, TRUE) == _browser_party_no_room, "room for three: no room for the party");
	CHECK(browser_party_room(4, 12, 16, FALSE) == _browser_party_unheld,
		"a host that holds nothing (Legacy, older OpenH1): room, but no guarantee");
	CHECK(browser_party_room(4, 13, 16, FALSE) == _browser_party_no_room, "nor room there");
	CHECK(browser_party_room(4, 0, 0, TRUE) == _browser_party_held, "no most players told: not taken as full");
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";

	if (!strcmp(name, "grant"))
		case_grant();
	else if (!strcmp(name, "expire"))
		case_expire();
	else if (!strcmp(name, "release"))
		case_release();
	else if (!strcmp(name, "admit"))
		case_admit();
	else if (!strcmp(name, "team"))
		case_team();
	else if (!strcmp(name, "room"))
		case_room();
	else
		return 2;
	return 0;
}
