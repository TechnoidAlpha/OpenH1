/*
PLAYER_SETTINGS.C

Each local player's own settings (Settings' field of view, its First
Person page's): config.toml's settings that are a player's (port_config.c's
table marks them _scope_player) can have a value of each player profile's
own, kept in the profile's folder beside its blam.sav (port_config.c's
settings.toml). Each frame the profile each local player plays with (the
player UI's active profile) is followed, and its folder named to
port_config.c as it changes, which reads that player's values from it: in
split screen each player's view is drawn as their own profile says. A
player with no profile, or a default one (the Xbox's, which has no folder
of its own), has config.toml's.

The readers ask for their local player's value (config_player_real and the
rest), keeping one for each local player (PLAYER_SETTINGS_SLOT).
*/

#include "cseries.h"
#include "player_settings.h"
#include "interface/player_ui.h"
#include "networking/network_connection.h"
#include "tag_files/files.h"
#include "saved games/player_profile.h"
#include "saved games/saved_game_files.h"

#include <string.h>

/* port/linux/src/port_config.c's (port_config.h) */
void config_player_profile(int local_player, const char *folder);
/* the platform layer's (platform.h) */
void platform_translate_path(const char *xbox_path, char *host_path, unsigned long host_path_size);

/* the profile each local player had at the last frame (NONE: none); the
first frame names each */
static struct
{
	boolean known;
	long profile_index;
} player_settings_players[MAXIMUM_NUMBER_OF_LOCAL_PLAYERS];

int player_settings_profile_folder(long profile_index, char *folder, unsigned long size)
{
	char xbox_path[MAXIMUM_FILENAME_LENGTH + 1];

	if (!size)
		return FALSE;
	folder[0] = 0;
	if (profile_index == NONE || TEST_FLAG(profile_index, _saved_game_file_index_read_only_bit) ||
		!player_profile_get_enclosing_directory_path(profile_index, xbox_path) || !xbox_path[0])
	{
		return FALSE;
	}
	platform_translate_path(xbox_path, folder, size);
	return folder[0] != 0;
}

void player_settings_update(void)
{
	short local_player_index;

	for (local_player_index = 0; local_player_index < MAXIMUM_NUMBER_OF_LOCAL_PLAYERS; local_player_index++)
	{
		long profile_index = player_ui_get_active_player_profile_index(local_player_index);
		char folder[1024];

		if (player_settings_players[local_player_index].known &&
			player_settings_players[local_player_index].profile_index == profile_index)
		{
			continue;
		}
		player_settings_players[local_player_index].known = TRUE;
		player_settings_players[local_player_index].profile_index = profile_index;
		if (!player_settings_profile_folder(profile_index, folder, sizeof(folder)))
			folder[0] = 0;
		config_player_profile(local_player_index, folder);
	}
}
