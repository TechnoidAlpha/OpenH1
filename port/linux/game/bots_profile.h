/* bots_profile.h: how people played a map, for the bots (port/linux/game/bots_profile.c)

A map's profile (port/assets/bots/<map>.txt, made from play logs by
tools/play_log/profile.py, built into the game as bots_profiles.inc) says
where people were and fought, the teleporters they took, where they picked
up weapons and how far off they fired each; a map with none takes the
general one, made from every map's logs, which has only the weapons' ranges
and the weapons people went for. bots.c reads it: its places to roam, the
teleporters it takes, the weapons it wants and the distance it keeps.

Needs cseries.h (real, boolean) and math/real_math.h (real_point3d) before
it. */
#ifndef HALO_BOTS_PROFILE_H
#define HALO_BOTS_PROFILE_H

enum
{
	BOTS_PROFILE_MAXIMUM_SPOTS = 64,
	BOTS_PROFILE_MAXIMUM_TELEPORTERS = 16,
	BOTS_PROFILE_MAXIMUM_PICKUPS = 64,
	BOTS_PROFILE_MAXIMUM_WEAPONS = 24,
	/* a weapon's name's size (a tag's name's last part, its spaces as _) */
	BOTS_PROFILE_NAME_SIZE = 32,
};

/* the map's profile (its scenario's name, a path: its last part), else the
general one; FALSE for none at all */
boolean bots_profile_load(char const *map_name);

/* the places people were (a weight of 1000 for the map's, and the share of
it they fired in, 0 to 1), and where they picked up weapons (a weight: how
many times); FALSE past the last */
boolean bots_profile_spot(long index, real_point3d *point, real *weight, real *fights);
boolean bots_profile_pickup(long index, real_point3d *point, real *weight);

/* the uses an hour a player of the teleporter people took whose entrance
is within 3 units of that point, 0 for none */
real bots_profile_teleporter_uses(real_point3d const *entrance);

/* how far off people fired the weapon (a tag's name: its last part is
looked for, spaces or _), the median in world units; FALSE for a weapon
with too few shots */
boolean bots_profile_range(char const *weapon_tag_name, real *range);

/* the share of the weapons people picked up that were this one (0 to 1:
the map's pickups, else the general profile's) */
real bots_profile_want(char const *weapon_tag_name);

#endif
