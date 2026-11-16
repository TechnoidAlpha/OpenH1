/*
LAST_PROFILE_LOOKUP.C (test)

Player 1's last used profile (source/interface/player_ui.c,
player_ui_get_player1_last_used_profile_index): lastprof.txt looked for
once, so that the menus' and the party's asking each frame does not read it
(and log its absence) each time; and not before the profiles are enumerated
(the mapfile, written on the filesystem thread, is the last run's until
then: #888). test_last_profile_lookup.py takes the code (under_test.inc).
*/

#include "harness.h"

enum { _saved_game_file_type_player_profile = 0 };

/* the fake world: the file there or not, and the reads of it; the
profiles enumerated or not */
static boolean file_there;
static int file_reads;
static boolean enumerated = TRUE;

static boolean saved_game_files_enumerated(void)
{
	return enumerated;
}

static boolean saved_game_file_retrieve_player1_last_used_profile_directory(char *directory_path)
{
	file_reads++;
	if (!file_there)
		return FALSE;
	strcpy(directory_path, "z:\\saved\\player_profiles\\00000003");
	return TRUE;
}

static long saved_game_file_find_profile_index_for_directory_path(char const *directory_path, long type)
{
	(void)type;
	return strstr(directory_path, "00000003") ? 3 : NONE;
}

static long player1_last_used_profile_index = NONE;
static char player1_profile_path[0x100] = { 0 };

#include "under_test.inc"

int main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "";
	int index;

	if (!strcmp(name, "missing"))
	{
		/* no profile chosen yet: looked for once, whatever asks */
		for (index = 0; index < 600; index++)
			CHECK(player_ui_get_player1_last_used_profile_index() == NONE, "a profile with no file");
		CHECK(file_reads == 1, "lastprof.txt read %d times", file_reads);
	}
	else if (!strcmp(name, "there"))
	{
		file_there = TRUE;
		for (index = 0; index < 600; index++)
			CHECK(player_ui_get_player1_last_used_profile_index() == 3, "the profile last used");
		CHECK(file_reads == 1, "lastprof.txt read %d times", file_reads);
	}
	else if (!strcmp(name, "before_enumeration"))
	{
		/* asked while the filesystem thread writes the mapfile: nothing read
		of it, and the profile found once it is written */
		file_there = TRUE;
		enumerated = FALSE;
		for (index = 0; index < 600; index++)
			CHECK(player_ui_get_player1_last_used_profile_index() == NONE, "a profile before the enumeration");
		CHECK(file_reads == 0, "lastprof.txt read %d times before the enumeration", file_reads);
		enumerated = TRUE;
		for (index = 0; index < 600; index++)
			CHECK(player_ui_get_player1_last_used_profile_index() == 3, "the profile last used");
		CHECK(file_reads == 1, "lastprof.txt read %d times", file_reads);
	}
	else
	{
		fprintf(stderr, "no case %s\n", name);
		return 2;
	}
	return 0;
}
