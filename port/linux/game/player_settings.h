/*
PLAYER_SETTINGS.H

Each local player's own settings (player_settings.c): config.toml's
settings that are a player's (port/linux/src/port_config.c's table marks
them _scope_player) at the values of the player profile each local player
plays with.
*/

#ifndef __PLAYER_SETTINGS_H
#define __PLAYER_SETTINGS_H

/* port/linux/src/port_config.c's (port_config.h): a setting as local player
0 to 3 sees it, their profile's value, else config.toml's; any other index
(NONE) config.toml's */
int config_per_player(const char *name);
int config_player_boolean(int local_player, const char *name);
long config_player_integer(int local_player, const char *name);
double config_player_real(int local_player, const char *name);
const char *config_player_string(int local_player, const char *name);
unsigned long config_changes(void);

/* the slot of a reader's values kept for each local player
(PLAYER_SETTINGS_SLOTS of them): the player's, and the last for none
(config.toml's) */
#define PLAYER_SETTINGS_SLOTS 5
#define PLAYER_SETTINGS_SLOT(local_player_index) \
	((local_player_index) >= 0 && (local_player_index) < PLAYER_SETTINGS_SLOTS - 1 ? \
		(local_player_index) : PLAYER_SETTINGS_SLOTS - 1)

/* each frame: each local player's profile followed (its folder named to
port_config.c as it changes) */
void player_settings_update(void);
/* the folder on this machine a player profile's own settings are kept in
(the saved game file's, beside its blam.sav); FALSE for none (NONE, a
default profile) */
int player_settings_profile_folder(long profile_index, char *folder, unsigned long size);

#endif
