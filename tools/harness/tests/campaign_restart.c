/*
CAMPAIGN_RESTART.C (test)

A mission in progress started again (port/linux/game/campaign_menu.inc's
campaign_restart_*, menu_functions.c's campaign_start and
campaign_continue, game_state.c's game_state_save_to_persistent_storage):
RESTART offered with a saved game (the Campaign menu's, its mission's card
on Choose Mission), its question's RESTART playing the mission from its
start at the saved game's difficulty, CANCEL changing nothing, and the
saved game's file kept until the new start's first checkpoint.
test_campaign_restart.py takes the code (under_test.inc).

The world: the profile's saved game on disk (its mission, difficulty and
which play wrote it), main.c's map change (main_set_map_name allows the
saved game, main_disallow_persistent_storage does not; main_new_map loads
it if allowed and of the difficulty played, as
game_state_try_and_load_from_persistent_storage does), and the game's
checkpoint in memory.
*/

#include "harness.h"

#define SOUND_ERROR 4
#define SOUND_CURSOR 1
#define SOUND_FORWARD 2
#define SOUND_BACK 3
#define GAME_STATE_SIZE 0x1000

struct player_profile
{
	int unused;
};

static char const *const level_names[] = { "a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40" };

/* the profile's saved game on disk */
static struct
{
	boolean exists;
	short level;
	short difficulty;
	/* which play wrote it (0: the one before the test's) */
	int play;
} disk;

/* main.c's */
static struct
{
	char map_name[64];
	short difficulty;
	boolean allow_persistent_storage;
	boolean switched;
} main_globals;

/* the game's: its play (each map change one more), and whether a
checkpoint of it has been written */
static int play;
static boolean checkpoint;
static boolean profile_present = TRUE;

static struct
{
	boolean custom;
	short chosen;
} campaign_cards;

static struct
{
	short saved_level;
	short saved_difficulty;
} campaign;

short player_spawn_count = 1;
boolean recover_saved_games_hack;

static struct
{
	void *base_address;
	struct { unsigned long checksum; } *header;
} game_state_globals;

static void player_ui_set_single_player_local_player_controller(short local_player, short controller)
{
	(void)local_player;
	(void)controller;
}

static void player_ui_remember_player1_profile(boolean remember)
{
	(void)remember;
}

static void ui_play_audio_feedback_sound(short sound)
{
	(void)sound;
}

static void game_connection_set(short connection)
{
	(void)connection;
}

static char const *main_get_solo_level_name(short level)
{
	return level >= 0 && level < 10 ? level_names[level] : "";
}

static void main_set_map_name(char const *map_name)
{
	strncpy(main_globals.map_name, map_name, sizeof(main_globals.map_name) - 1);
	main_globals.allow_persistent_storage = TRUE;
}

static void main_set_difficulty(short difficulty)
{
	main_globals.difficulty = difficulty;
}

static void main_menu_switch_to_single_player(void)
{
	main_globals.switched = TRUE;
}

static void main_disallow_persistent_storage(void)
{
	main_globals.allow_persistent_storage = FALSE;
}

static boolean campaign_profile(short controller, struct player_profile *profile)
{
	(void)controller;
	(void)profile;
	return profile_present;
}

/* game_state_test_persistent_storage, through the menus' check */
static boolean ui_widget_port_saved_game(char const **map_name, short *level, short *difficulty)
{
	if (!disk.exists)
		return FALSE;
	*map_name = level_names[disk.level];
	*level = disk.level;
	*difficulty = disk.difficulty;
	return TRUE;
}

static void game_state_note_event(char const *event)
{
	(void)event;
}

/* (port/linux/game/demo.c: no demo plays) */
static boolean demo_playing(void)
{
	return FALSE;
}

static boolean game_state_port_saved_game_valid(void)
{
	return checkpoint;
}

static void game_state_revert(void)
{
}

/* the game state (at its last checkpoint: game_state_revert) written as the
profile's saved game */
static void game_state_write_to_persistent_storage(void *buffer, unsigned long *header_checksum, long header_size,
	long buffer_size)
{
	short level;

	(void)buffer;
	(void)header_checksum;
	(void)header_size;
	(void)buffer_size;
	for (level = 0; level < 10 && strcmp(level_names[level], main_globals.map_name); level++)
		;
	disk.exists = TRUE;
	disk.level = level;
	disk.difficulty = main_globals.difficulty;
	disk.play = play;
}

/* the saved game is never an MCC map's here (menu_functions.c's
mcc_saved_game_read: the profile's saved game named an MCC map), so
CONTINUE goes on as it does for the game's own maps */
static boolean mcc_saved_game_read(char name[256], short *difficulty)
{
	(void)difficulty;
	name[0] = 0;
	return FALSE;
}

static boolean mcc_cache_require(char const *name, unsigned long checksum)
{
	(void)name;
	(void)checksum;
	return FALSE;
}

#include "under_test.inc"

/* the map change (main.c's main_new_map): TRUE if the saved game was loaded */
static boolean new_map(void)
{
	CHECK(main_globals.switched, "no mission started");
	main_globals.switched = FALSE;
	play++;
	checkpoint = FALSE;
	return main_globals.allow_persistent_storage && disk.exists &&
		!strcmp(level_names[disk.level], main_globals.map_name) && disk.difficulty == main_globals.difficulty;
}

/* a profile whose saved game is in the fourth mission, on Heroic */
static void saved_in_progress(void)
{
	memset(&main_globals, 0, sizeof(main_globals));
	disk.exists = TRUE;
	disk.level = 3;
	disk.difficulty = 2;
	disk.play = 0;
	play = 0;
	checkpoint = FALSE;
	profile_present = TRUE;
	campaign.saved_level = 3;
	campaign.saved_difficulty = 2;
	campaign_cards.custom = FALSE;
	campaign_cards.chosen = 3;
	campaign_restart_level = NONE;
}

static void offered(void)
{
	CHECK(campaign_restart_offered(3, FALSE, NONE), "the Campaign menu offered no RESTART MISSION with a saved game");
	CHECK(campaign_restart_offered(3, TRUE, NONE), "the Campaign menu's RESTART MISSION hidden after CUSTOM was shown");
	CHECK(campaign_restart_offered(3, FALSE, 3), "the saved game's card offered no RESTART");
	CHECK(!campaign_restart_offered(3, FALSE, 4), "RESTART offered on a card that has no saved game");
	CHECK(!campaign_restart_offered(3, TRUE, 3), "RESTART offered on a CUSTOM map's card");
	CHECK(!campaign_restart_offered(NONE, FALSE, NONE), "RESTART MISSION offered with no saved game");
	CHECK(!campaign_restart_offered(NONE, FALSE, 0), "RESTART offered on a card with no saved game");
	saved_in_progress();
	campaign_cards.chosen = 5;
	CHECK(!campaign_restart_ask(campaign_cards.chosen), "Y on a card without the saved game asked");
	CHECK(campaign_restart_level == NONE, "a question was left asked");
	campaign.saved_level = NONE;
	CHECK(!campaign_restart_ask(NONE), "RESTART MISSION asked with no saved game");
}

static void confirm_fresh_start(void)
{
	short chosen;

	for (chosen = NONE; chosen <= 3; chosen += 4)
	{
		saved_in_progress();
		CHECK(campaign_restart_ask(chosen), "the question did not open (%d)", chosen);
		CHECK(campaign_restart(0), "RESTART started nothing (%d)", chosen);
		CHECK(!strcmp(main_globals.map_name, "b30"), "RESTART played %s, not the saved game's mission",
			main_globals.map_name);
		CHECK(main_globals.difficulty == 2, "RESTART played on difficulty %d, not the saved game's",
			main_globals.difficulty);
		CHECK(!new_map(), "RESTART went on with the saved game, not the mission's start");
		CHECK(disk.exists && disk.play == 0, "the saved game was lost as the mission started again");
	}
}

/* (the world's map change sees the difference: CONTINUE goes on with it) */
static void continue_goes_on(void)
{
	saved_in_progress();
	CHECK(campaign_continue(0), "CONTINUE started nothing");
	CHECK(new_map(), "CONTINUE did not go on with the saved game");
}

static void cancel_keeps(void)
{
	saved_in_progress();
	CHECK(campaign_restart_ask(NONE), "the question did not open");
	CHECK(campaign_restart_cancel(), "CANCEL failed");
	CHECK(!main_globals.switched, "CANCEL started a mission");
	CHECK(disk.exists && disk.level == 3 && disk.difficulty == 2 && disk.play == 0, "CANCEL changed the saved game");
	/* (a RESTART not asked again: nothing) */
	CHECK(!campaign_restart(0), "a RESTART after CANCEL started the mission");
	CHECK(!main_globals.switched, "a RESTART after CANCEL started the mission");
	/* (the saved game no longer the one asked about: nothing) */
	saved_in_progress();
	CHECK(campaign_restart_ask(NONE), "the question did not open");
	disk.level = 5;
	CHECK(!campaign_restart(0), "RESTART started another mission than the one asked about");
	CHECK(!main_globals.switched, "RESTART started another mission than the one asked about");
}

static void kept_until_checkpoint(void)
{
	saved_in_progress();
	CHECK(campaign_restart_ask(NONE) && campaign_restart(0), "RESTART started nothing");
	CHECK(!new_map(), "RESTART went on with the saved game");
	/* (left before the new start's first checkpoint: the old one kept) */
	game_state_save_to_persistent_storage();
	CHECK(disk.exists && disk.play == 0 && disk.level == 3, "the saved game was lost before a checkpoint took its place");
	/* (played to its first checkpoint, then left: the new start's) */
	checkpoint = TRUE;
	game_state_save_to_persistent_storage();
	CHECK(disk.exists && disk.play == play, "the new start's checkpoint did not take the saved game's place");
}

int main(int argc, char **argv)
{
	char const *name = argc > 1 ? argv[1] : "";

	game_state_globals.header = calloc(1, sizeof(*game_state_globals.header));
	if (!strcmp(name, "offered"))
		offered();
	else if (!strcmp(name, "confirm-fresh-start"))
		confirm_fresh_start();
	else if (!strcmp(name, "continue-goes-on"))
		continue_goes_on();
	else if (!strcmp(name, "cancel-keeps"))
		cancel_keeps();
	else if (!strcmp(name, "kept-until-checkpoint"))
		kept_until_checkpoint();
	else
		return 2;
	return 0;
}
