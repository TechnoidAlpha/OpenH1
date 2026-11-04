/*
BOTS_PROFILE.C

How people played a map, for the bots (bots_profile.h): the profiles of
port/assets/bots, made from play logs by tools/play_log/profile.py and
built in as bots_profiles.inc, read once a map. Each is lines of words:

    map <name> <games> <player-minutes>
    spot <x> <y> <z> <weight> <fights>
    teleporter <x> <y> <z> <uses>
    pickup <weapon> <x> <y> <z> <count>
    range <weapon> <median> <p25> <p75> <shots>
    want <weapon> <share of 1000>

(tools/play_log/profile.py says what each is.) A line not understood is
passed over.
*/

#include "cseries.h"
#include "math/real_math.h"

#include "bots_profile.h"

#include <stdio.h>
#include <string.h>

/* port/linux/src/port_config.c */
void platform_log(char const *format, ...);

struct bots_profile_text
{
	char const *map;
	char const *text;
};

static struct bots_profile_text const bots_profile_texts[] =
{
#include "bots_profiles.inc"
};

struct bots_profile_weapon
{
	char name[BOTS_PROFILE_NAME_SIZE];
	real range;
	real want;
};

static struct
{
	long spot_count;
	real_point3d spots[BOTS_PROFILE_MAXIMUM_SPOTS];
	real spot_weights[BOTS_PROFILE_MAXIMUM_SPOTS];
	real spot_fights[BOTS_PROFILE_MAXIMUM_SPOTS];
	long teleporter_count;
	real_point3d teleporters[BOTS_PROFILE_MAXIMUM_TELEPORTERS];
	real teleporter_uses[BOTS_PROFILE_MAXIMUM_TELEPORTERS];
	long pickup_count;
	real_point3d pickups[BOTS_PROFILE_MAXIMUM_PICKUPS];
	real pickup_weights[BOTS_PROFILE_MAXIMUM_PICKUPS];
	char pickup_names[BOTS_PROFILE_MAXIMUM_PICKUPS][BOTS_PROFILE_NAME_SIZE];
	real pickup_total;
	long weapon_count;
	struct bots_profile_weapon weapons[BOTS_PROFILE_MAXIMUM_WEAPONS];
} bots_profile;

/* a tag's name's last part, its spaces as _ (the profiles' weapon names) */
static void bots_profile_weapon_name(char const *tag_name, char *name)
{
	char const *last = tag_name;
	char const *character;
	long length = 0;

	for (character = tag_name; *character; character++)
	{
		if (*character == '\\' || *character == '/')
			last = character + 1;
	}
	for (; *last && length < BOTS_PROFILE_NAME_SIZE - 1; last++)
		name[length++] = *last == ' ' ? '_' : *last;
	name[length] = 0;
}

static struct bots_profile_weapon *bots_profile_weapon(char const *name, boolean add)
{
	long index;

	for (index = 0; index < bots_profile.weapon_count; index++)
	{
		if (!strcmp(bots_profile.weapons[index].name, name))
			return &bots_profile.weapons[index];
	}
	if (!add || bots_profile.weapon_count >= BOTS_PROFILE_MAXIMUM_WEAPONS)
		return NULL;
	index = bots_profile.weapon_count++;
	csmemset(&bots_profile.weapons[index], 0, sizeof(bots_profile.weapons[index]));
	strncpy(bots_profile.weapons[index].name, name, BOTS_PROFILE_NAME_SIZE - 1);
	return &bots_profile.weapons[index];
}

/* one line of a profile; the weapons' ranges and wants only (keep_places
FALSE: the general profile's under a map's) */
static void bots_profile_line(char const *line, boolean keep_places)
{
	char word[16], name[BOTS_PROFILE_NAME_SIZE];
	float a, b, c, d, e;

	if (sscanf(line, "%15s", word) != 1)
		return;
	if (keep_places && !strcmp(word, "spot") && bots_profile.spot_count < BOTS_PROFILE_MAXIMUM_SPOTS &&
		sscanf(line, "%*s %f %f %f %f %f", &a, &b, &c, &d, &e) == 5)
	{
		long index = bots_profile.spot_count++;

		bots_profile.spots[index].x = a;
		bots_profile.spots[index].y = b;
		bots_profile.spots[index].z = c;
		bots_profile.spot_weights[index] = d;
		bots_profile.spot_fights[index] = e / 100.0f;
	}
	else if (keep_places && !strcmp(word, "teleporter") &&
		bots_profile.teleporter_count < BOTS_PROFILE_MAXIMUM_TELEPORTERS &&
		sscanf(line, "%*s %f %f %f %f", &a, &b, &c, &d) == 4)
	{
		long index = bots_profile.teleporter_count++;

		bots_profile.teleporters[index].x = a;
		bots_profile.teleporters[index].y = b;
		bots_profile.teleporters[index].z = c;
		bots_profile.teleporter_uses[index] = d;
	}
	else if (keep_places && !strcmp(word, "pickup") && bots_profile.pickup_count < BOTS_PROFILE_MAXIMUM_PICKUPS &&
		sscanf(line, "%*s %31s %f %f %f %f", name, &a, &b, &c, &d) == 5)
	{
		long index = bots_profile.pickup_count++;

		bots_profile.pickups[index].x = a;
		bots_profile.pickups[index].y = b;
		bots_profile.pickups[index].z = c;
		bots_profile.pickup_weights[index] = d;
		strncpy(bots_profile.pickup_names[index], name, BOTS_PROFILE_NAME_SIZE - 1);
		bots_profile.pickup_names[index][BOTS_PROFILE_NAME_SIZE - 1] = 0;
		bots_profile.pickup_total += d;
	}
	else if (!strcmp(word, "range") && sscanf(line, "%*s %31s %f", name, &a) == 2)
	{
		struct bots_profile_weapon *weapon = bots_profile_weapon(name, TRUE);

		/* (the map's, read first, over the general one's) */
		if (weapon && weapon->range <= 0.0f)
			weapon->range = a;
	}
	else if (!strcmp(word, "want") && sscanf(line, "%*s %31s %f", name, &a) == 2)
	{
		struct bots_profile_weapon *weapon = bots_profile_weapon(name, TRUE);

		if (weapon && weapon->want <= 0.0f)
			weapon->want = a / 1000.0f;
	}
}

static void bots_profile_read(char const *text, boolean keep_places)
{
	char line[128];

	while (*text)
	{
		long length = 0;

		while (*text && *text != '\n')
		{
			if (length < (long)sizeof(line) - 1)
				line[length++] = *text;
			text++;
		}
		if (*text)
			text++;
		line[length] = 0;
		bots_profile_line(line, keep_places);
	}
}

static char const *bots_profile_find(char const *map)
{
	long index;

	for (index = 0; index < (long)NUMBEROF(bots_profile_texts); index++)
	{
		if (!strcmp(bots_profile_texts[index].map, map))
			return bots_profile_texts[index].text;
	}
	return NULL;
}

boolean bots_profile_load(char const *map_name)
{
	char map[64];
	char const *text, *general = bots_profile_find("general");
	long index;

	csmemset(&bots_profile, 0, sizeof(bots_profile));
	bots_profile_weapon_name(map_name ? map_name : "", map);
	text = bots_profile_find(map);
	if (text)
		bots_profile_read(text, TRUE);
	if (general)
		bots_profile_read(general, FALSE);
	/* (the map's pickups say what people want on it: over the general
	profile's) */
	if (bots_profile.pickup_total > 0.0f)
	{
		for (index = 0; index < bots_profile.weapon_count; index++)
			bots_profile.weapons[index].want = 0.0f;
		for (index = 0; index < bots_profile.pickup_count; index++)
		{
			struct bots_profile_weapon *weapon = bots_profile_weapon(bots_profile.pickup_names[index], TRUE);

			if (weapon)
				weapon->want += bots_profile.pickup_weights[index] / bots_profile.pickup_total;
		}
	}
	platform_log("bots: %s profile: %ld places, %ld teleporters, %ld pickups, %ld weapons",
		text ? map : general ? "the general" : "no", bots_profile.spot_count, bots_profile.teleporter_count,
		bots_profile.pickup_count, bots_profile.weapon_count);
	return text || general;
}

boolean bots_profile_spot(long index, real_point3d *point, real *weight, real *fights)
{
	if (index < 0 || index >= bots_profile.spot_count)
		return FALSE;
	*point = bots_profile.spots[index];
	*weight = bots_profile.spot_weights[index];
	*fights = bots_profile.spot_fights[index];
	return TRUE;
}

boolean bots_profile_pickup(long index, real_point3d *point, real *weight)
{
	if (index < 0 || index >= bots_profile.pickup_count)
		return FALSE;
	*point = bots_profile.pickups[index];
	*weight = bots_profile.pickup_weights[index];
	return TRUE;
}

real bots_profile_teleporter_uses(real_point3d const *entrance)
{
	long index;

	for (index = 0; index < bots_profile.teleporter_count; index++)
	{
		real dx = bots_profile.teleporters[index].x - entrance->x;
		real dy = bots_profile.teleporters[index].y - entrance->y;
		real dz = bots_profile.teleporters[index].z - entrance->z;

		if (dx * dx + dy * dy + dz * dz < 3.0f * 3.0f)
			return bots_profile.teleporter_uses[index];
	}
	return 0.0f;
}

boolean bots_profile_range(char const *weapon_tag_name, real *range)
{
	char name[BOTS_PROFILE_NAME_SIZE];
	struct bots_profile_weapon *weapon;

	bots_profile_weapon_name(weapon_tag_name ? weapon_tag_name : "", name);
	weapon = bots_profile_weapon(name, FALSE);
	if (!weapon || weapon->range <= 0.0f)
		return FALSE;
	*range = weapon->range;
	return TRUE;
}

real bots_profile_want(char const *weapon_tag_name)
{
	char name[BOTS_PROFILE_NAME_SIZE];
	struct bots_profile_weapon *weapon;

	bots_profile_weapon_name(weapon_tag_name ? weapon_tag_name : "", name);
	weapon = bots_profile_weapon(name, FALSE);
	return weapon ? weapon->want : 0.0f;
}
