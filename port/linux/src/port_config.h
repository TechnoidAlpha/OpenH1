/*
PORT_CONFIG.H

The native ports' settings, read from config.toml (port_config.c): next to
the executable on the desktop, in the data folder (the one holding maps/)
on Android. A missing file is written with every setting commented out at
its default ("# vsync = true": a newer version's default reaches it), as
are a newer version's settings in an older file. Each setting can
also be set for one run with its HALO_* environment variable, which wins
over the file (the tools and the Android app pass settings that way).

Settings are named "section.key", as in the file: "display.vsync".
*/

#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

#include <stddef.h>

int config_boolean(const char *name);
long config_integer(const char *name);
double config_real(const char *name);
/* never NULL; "" when unset */
const char *config_string(const char *name);
/* debug.fresh_memory_fill: the byte (0x00, 0xAA) the allocators fill what
they give out uncleared with, or -1 for the game's own fill */
int halo_fresh_memory_fill(void);
/* sets a setting from its value as text ("true", "60", "1.5", "all"), and
writes it into config.toml (only its line changes); 1 on success */
int config_write(const char *name, const char *value);
/* sets a setting for this run alone, from its value as text, config.toml
left as it is (the menu studio's input and window: menu_studio.c) */
int config_set_for_run(const char *name, const char *value);
int config_write_boolean(const char *name, int value);
/* a setting's value as text ("true", "60", "1.5", "all"); 0 if there is no
such setting */
int config_text(const char *name, char *text, size_t size);
/* the folder config.toml is in, with its separator */
void config_folder(char *path, size_t size);
/* a whole file (a UTF-8 path), NUL terminated, or NULL; free() it (the Android
guest has no SDL_LoadFile) */
char *config_file_read(const char *path, size_t *size);
/* a setting's default, as text; 0 if there is no such setting */
int config_default(const char *name, char *text, size_t size);
/* the value display.graphics's preset (graphics: "classic", else
"enhanced") gives a setting it chooses, as text ("16", "smaa"); 0 if it
does not choose it */
int config_graphics_value(const char *graphics, const char *name, char *text, size_t size);
/* the name of the entry'th setting display.graphics chooses, NULL past the
last */
const char *config_graphics_setting(int entry);
/* how many times config_write has changed a setting: what keeps one reads
it again when this moves */
unsigned long config_changes(void);
/* battery saver on or off (battery_saver.c): while on, the settings
display.graphics chooses that the player has not set are read at their
"classic" values (config_boolean, config_integer, config_string; not
config_text); config_changes moves as it changes */
void config_battery_saver_set(int saving);

/* whether this is a dedicated server (server.enabled, --server,
HALO_SERVER: port/linux/src/server_mode.c), whose settings the file holds */
int config_server_mode_enabled(void);

/* a setting, for a list of them (a dedicated server's Settings page): the
one at index; 0 past the last */
struct config_setting_info
{
	const char *name;
	/* "boolean", "integer", "real" or "string" */
	const char *type;
	/* NULL: none */
	const char *environment;
	const char *comment;
	/* as text, a string's without its quotes */
	char default_text[300];
	/* its environment variable sets it for this run */
	int pinned;
	/* this build writes it into config.toml */
	int written;
};
int config_setting_info(int index, struct config_setting_info *info);

/* each weapon's own first-person offsets, by its tag's path ("weapons\\pistol\\pistol",
any case, / for \\): config.toml's [display.viewmodel_weapon."<path>"] tables,
x, y and z in centimeters and pitch, yaw and roll in degrees, added to the
global ones (view_fov.c). config_viewmodel_weapon gives them (0 and 0 if the
weapon has none); config_viewmodel_weapon_write sets them, for now and in
config.toml (all 0: the weapon's table taken out), 1 on success.
config_changes moves with a write */
#define CONFIG_WEAPON_VALUES 6
int config_viewmodel_weapon(const char *path, double values[CONFIG_WEAPON_VALUES]);
int config_viewmodel_weapon_write(const char *path, const double values[CONFIG_WEAPON_VALUES]);

/* each local player's own settings (port_config.c's table marks them
_scope_player: the field of view, the first-person view's): a player
profile's own values, in its folder's settings.toml, else config.toml's.
config_player_profile says which profile's folder (a path of this machine;
NULL or "": none) local player 0 to 3 plays with (port/linux/game/
player_settings.c); config_player_boolean and the rest read a setting as
that player sees it (config.toml's for a local player out of range, a
setting that is the machine's, one its environment variable sets, or the
menus' pictures' stock view). config_profile_text gives a profile's own
value (0: it has none, so config.toml's is its), config_profile_write sets
it, for now and in the file. config_changes moves with each */
int config_per_player(const char *name);
void config_player_profile(int local_player, const char *folder);
int config_player_boolean(int local_player, const char *name);
long config_player_integer(int local_player, const char *name);
double config_player_real(int local_player, const char *name);
/* never NULL */
const char *config_player_string(int local_player, const char *name);
int config_profile_text(const char *folder, const char *name, char *text, size_t size);
int config_profile_write(const char *folder, const char *name, const char *value);
/* whether local player 0 to 3's profile has a value of its own of the
setting (not one its environment variable sets); and whether a setting's
environment variable sets it for this run */
int config_player_has(int local_player, const char *name);
int config_pinned_by_environment(const char *name);
/* whether a setting is each player's of the keyboard and mouse, which only
player 1 plays with: player 1's profile's */
int config_keyboard_setting(const char *name);
/* each weapon's own offsets (config_viewmodel_weapon's, below) as a local
player sees them: their profile's table of the weapon, else config.toml's;
a profile's own (0: it has none), and set in it (its 0s kept, in place of
config.toml's) */
int config_player_viewmodel_weapon(int local_player, const char *path, double values[CONFIG_WEAPON_VALUES]);
int config_profile_viewmodel_weapon(const char *folder, const char *path, double values[CONFIG_WEAPON_VALUES]);
int config_profile_viewmodel_weapon_write(const char *folder, const char *path,
	const double values[CONFIG_WEAPON_VALUES]);

/* the settings whose names begin with prefix ("server.") read from
config.toml again, but for those their environment variables set; 1 on
success (a file that cannot be read leaves them as they were) */
int config_reload(const char *prefix);


#endif
