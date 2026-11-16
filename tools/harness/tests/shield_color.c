/*
SHIELD_COLOR.C

display.shield_color (port/linux/game/shield_color.c): a local player's
own unit's shield drawn in their chosen color, each split screen player's
their own (their profile's), any other unit's and a player's who chose
none in the shield's own.
*/

#include "harness.h"
#include <string.h>

#define MAXIMUM_NUMBER_OF_LOCAL_PLAYERS 4
#define NUMBEROF(a) (sizeof(a) / sizeof((a)[0]))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define csmemcpy memcpy

typedef struct { real red, green, blue; } real_rgb_color;
struct player_datum { long unit_index; };

/* two local players, units 100 and 101; each one's color, as their profile
or config.toml has it */
static struct player_datum fake_players[2] = { { 100 }, { 101 } };
static char const *fake_colors[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS] = { "default", "default", "default", "default" };
static unsigned long fake_changes = 1;

static unsigned long config_changes(void) { return fake_changes; }
static char const *config_player_string(int local_player, char const *name)
{
	(void)name;
	return local_player >= 0 && local_player < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS ? fake_colors[local_player] : "default";
}
static long local_player_get_player_index(short local_player_index)
{
	return local_player_index < 2 ? local_player_index : NONE;
}
static struct player_datum *player_get(long player_index) { return &fake_players[player_index]; }
/* (the profile's colors: the index's own red) */
static real_rgb_color player_profile_get_rgb_color(long index)
{
	real_rgb_color color = { (real)index, 0.f, 0.f };

	return color;
}

#include "under_test.inc"

static void colors(char const *first, char const *second)
{
	fake_colors[0] = first;
	fake_colors[1] = second;
	fake_changes++;
}

/* the color a unit's shield is drawn in: the chosen one's index, or -1 the
shield's own */
static long drawn(long unit_index)
{
	static real_rgb_color const own[4];
	real_rgb_color chosen;
	real_rgb_color const *given = shield_color_colors(unit_index, own);

	if (!shield_color_override(given, &chosen))
		return given == own ? -1 : -2;
	return (long)chosen.red;
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";

	CASE("players")
	{
		/* player 1 red (2), player 2 the shield's own, then cyan (9) */
		colors("red", "default");
		CHECK(drawn(100) == 2, "player 1's: %ld", drawn(100));
		CHECK(drawn(101) == -1, "player 2's: %ld", drawn(101));
		CHECK(drawn(102) == -1, "another unit's: %ld", drawn(102));
		colors("red", "cyan");
		CHECK(drawn(100) == 2 && drawn(101) == 9, "player 1's %ld, player 2's %ld", drawn(100), drawn(101));
		colors("default", "default");
		CHECK(drawn(100) == -1 && drawn(101) == -1, "none chosen: %ld, %ld", drawn(100), drawn(101));
	}
	return 0;
}
