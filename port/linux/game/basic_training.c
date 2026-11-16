/*
BASIC_TRAINING.C

The VR build's VR Tutorial (port/linux/README.md, "VR Tutorial";
issue #143): a drill on the player's own Blood Gulch, one thing at a time,
each chapter ending in a small challenge that uses all of it so far.

The main menu's VR TUTORIAL (the VR build's alone, while it plays in the
headset: tools/port_settings.py's _training) lists the chapters, a tick on
each one finished; a chapter chosen starts a game of this machine alone
(a local game, as Multiplayer's PLAY WITH BOTS: no other machine, nothing
sent) of Team Slayer on Blood Gulch with no score or time to end it, and
four bots (bots.c's bots_set_training: three against the player, one with
them), which this file drives. The pause menu's CHAPTERS opens the same
list (menu_tags.c's pause_list_patch), to jump forwards or back to any
chapter; and on the HUD, the D-pad's right skips to the next chapter and
its left goes back one. Jumping in sets the chapter up as it would be had
the ones before it been played: the player put at its place with its
weapons and grenades, the bots at their posts with their orders.

| # | Chapter | Learn | Ends with |
| 0 | Fit check | turning, the comfort vignette, seated or standing, height | the choices made |
| 1 | Boots on the ground | look, move | three marked spots run to |
| 2 | Weapons hot | aim, fire, reload | three still targets down on one magazine |
| 3 | Loadout | the foregrip, switch, pick up and swap | the sniper rifle and the pistol each hitting the target |
| 4 | Hands on | the grenade's throw, the melee | a grenade on a group, then a blow |
| 5 | Eyes up | the scope | (the next drop) |
| 6 | Mount up | vehicles | (the next drop) |
| 7 | Live fire | everything | one against one, then two against two with a bot teammate |

Each chapter is steps (basic_training_steps), each with a check of what
the player did since it began (basic_training_step_result, from what this
file sees of their unit each tick: basic_training_seen) and the set up it
needs (the bots' orders, things placed). A step's words are the HUD's own
text, in the HUD's font and colour, in a light drill instructor's voice;
a button they name is named as the HUD's prompts name it in VR (the input
on the controllers in the hands, as the OpenXR runtime names it:
port/linux/src/vr.c's halo_vr_button_name), and lit on the controller in the
hand (vr_hands.c draws a pulsing ring there, with a short buzz of that hand
as the step begins). Done, a step says so on the HUD with the game's own
beep; a chapter done, with the game's countdown's end, and the next one
starts. A challenge failed (the magazine dry with targets standing, the
other side first to the score) is set up again.

Finished chapters are kept in config.toml (vr.training_finished, the
chapters' numbers). debug.training ("<chapter>[:<step>]") starts a training
game at that chapter (and step) from the main menu, for tests and
captures.

Only this machine's game: nothing goes to another, and the training is
never offered in a network game.
*/

#include "cseries.h"
#include "main/main.h"
#include "bink/bink_playback.h"
#include "interface/player_ui.h"
#include "interface/ui_widget.h"
#include "interface/hud.h"
#include "interface/hud_messaging.h"
#include "networking/network_game_globals.h"
#include "networking/network_client_manager.h"
#include "networking/network_server_manager.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "objects/objects.h"
#include "units/units.h"
#include "items/weapons.h"
#include "items/weapon_definitions.h"
#include "input/input.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "scenario/scenario_definitions.h"
#include "tag_files/tag_groups.h"
#include "tag_files/tag_files.h"
#include "cache/cache_files.h"
#include "cutscene/cinematics.h"
#include "rasterizer/rasterizer.h"
#include "render/render.h"
#include "text/draw_string.h"
#include "text/font_group.h"
#include "text/unicode.h"
#include "math/real_math.h"
#include "input/input_abstraction.h"

#include "bots.h"
#include "controller_remap.h"
#include "game_modes.h"
#include "basic_training.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the platform layer's (port/linux/src/port_config.c) */
const char *config_string(char const *name);
int config_write(char const *name, char const *value);
void platform_log(char const *format, ...);
void draw_quad(rectangle2d *rectangle, pixel32 color);

#ifdef HALO_VR
/* port/linux/src/vr.c (vr.h) */
int halo_vr_running(void);
int halo_vr_button_name(int button, char *name, unsigned long size);
void halo_vr_rumble(float left, float right);
int halo_vr_left_handed(void);
int halo_vr_head(float forward[3], float up[3], float position[3]);
#endif

/* ---------- constants */

#define TRAINING_PI 3.14159265f

/* the game's multiplayer sounds (game_engine_multiplayer_sounds.c's
multiplayer_information_sound): a beep of the countdown, its end, the
announcer's "Slayer" and "Team Slayer" */
enum
{
	_training_sound_slayer = 21,
	_training_sound_countdown_timer = 26,
	_training_sound_team_slayer = 35,
	_training_sound_countdown_timer_end = 42,
};

/* the controller buttons as halo_vr_button_name numbers them (the game's:
A, B, X, Y, the black and white (the bumpers or grips), the triggers, the
D-pad, Start, Back, the sticks' clicks) */
enum
{
	_training_button_a = 0,
	_training_button_b,
	_training_button_x,
	_training_button_y,
	_training_button_right_bumper,
	_training_button_left_bumper,
	_training_button_left_trigger,
	_training_button_right_trigger,
	_training_button_dpad_up,
	_training_button_dpad_down,
	_training_button_dpad_left,
	_training_button_dpad_right,
	_training_button_menu,
	_training_button_view,
	_training_button_left_stick,
	_training_button_right_stick,
	_training_button_left_grip = BASIC_TRAINING_LEFT_GRIP,
	NUMBER_OF_TRAINING_BUTTONS
};

/* what finishes a step (basic_training_step_result), of what the player did
since it began; amount is its figure */
enum
{
	/* turned so many degrees, all told */
	_training_check_turn = 0,
	/* looked so many degrees up */
	_training_check_look_up,
	/* walked so many world units */
	_training_check_move,
	/* the fit check's choices made (its screen closed; out of the headset,
	at once) */
	_training_check_fit,
	/* every one of the marked spots reached */
	_training_check_spots,
	/* so many rounds fired */
	_training_check_fire,
	/* a reload */
	_training_check_reload,
	/* the weapon in the hands switched */
	_training_check_switch,
	/* the long gun's foregrip held (out of the headset, at once) */
	_training_check_grip,
	/* so many targets down before the magazine ran dry */
	_training_check_targets,
	/* the sniper rifle and the pistol held, the target hit with each */
	_training_check_loadout,
	/* a grenade thrown */
	_training_check_grenade,
	/* a blow struck on a bot */
	_training_check_melee,
	/* a grenade's blast on so many of the group, and a blow on one */
	_training_check_group,
	/* so many kills before the other side has as many */
	_training_check_score,
	NUMBER_OF_TRAINING_CHECKS
};

/* what a step sets up as it begins (and again, a challenge failed) */
enum
{
	/* every bot out of the game */
	_training_setup_clear = 0,
	/* the marked spots */
	_training_setup_spots,
	/* the range: a bot standing far out, to shoot at */
	_training_setup_range,
	/* three still targets downrange, and one full magazine */
	_training_setup_targets,
	/* the sniper rifle and the pistol on the ground ahead, and a target
	going side to side */
	_training_setup_loadout,
	/* a bot standing close, for a blow; grenades */
	_training_setup_dummy,
	/* three bots together behind cover, peeking out, and four grenades */
	_training_setup_group,
	/* one against one: a bot that fights back, gently */
	_training_setup_duel,
	/* two against two, a bot with the player */
	_training_setup_team,
	NUMBER_OF_TRAINING_SETUPS
};

/* the weapons a chapter starts with */
enum
{
	_training_loadout_rifle = 0,
	_training_loadout_rifle_plasma,
	_training_loadout_rifle_pistol,
	NUMBER_OF_TRAINING_LOADOUTS
};

/* a step's result */
enum
{
	_training_result_waiting = 0,
	_training_result_done,
	_training_result_failed,
};

/* the bots (bots.c: the first three against the player, the fourth with
them) */
#define TRAINING_BOTS 4
#define TRAINING_TEAMMATE 3
/* the chapters not yet made (the next drop's set pieces) */
#define TRAINING_CHAPTER_SNIPER 5
#define TRAINING_CHAPTER_WARTHOG 6
/* a spot is reached this near (world units, along the ground) */
#define TRAINING_SPOT_REACH 1.5f
#define TRAINING_SPOTS 3
/* after a chapter is done, the next one starts this many seconds later;
a failed challenge is set up again after this many */
#define TRAINING_NEXT_CHAPTER_SECONDS 4.0f
#define TRAINING_RETRY_SECONDS 2.5f
/* a hit on a bot is the grenade's this long after a throw, the blow's this
long after a swing (ticks), the weapon's this long after a shot */
#define TRAINING_GRENADE_TICKS (TICKS_PER_SECOND * 4)
#define TRAINING_MELEE_TICKS (TICKS_PER_SECOND / 2)
#define TRAINING_SHOT_TICKS (TICKS_PER_SECOND / 3)
/* the HUD's line of a step done, shown this long (seconds) */
#define TRAINING_FEEDBACK_SECONDS 3.0f
/* the starting game's stages' times (seconds from the start), as
network_test.c's local game has them */
#define TRAINING_START_MAP_SECONDS 1.0f
#define TRAINING_START_PLAYER_SECONDS 2.0f
#define TRAINING_START_GO_SECONDS 3.0f
#define TRAINING_MAP "levels\\test\\bloodgulch\\bloodgulch"
#define TRAINING_MAP_NAME "bloodgulch"
/* the fit check's screen (tools/port_settings.py's VR_SCREENS["vr_fit_check"]) */
#define TRAINING_FIT_SCREEN "pc\\main_menu\\settings_select\\player_setup\\player_profile_edit\\vr_fit_check\\vr_fit_check_screen"

/* ---------- structures */

struct basic_training_chapter
{
	char const *name;
	char const *about;
	/* where the player starts it (and respawns), in the field's frame
	(basic_training_field_point: along the canyon from the red flag,
	left of that), turned this far left of up the canyon (degrees) */
	real along;
	real left;
	real facing;
	short loadout;
	/* its frag grenades */
	short grenades;
};

struct basic_training_step
{
	short chapter;
	short check;
	short amount;
	short setup;
	/* the button the words name ("%s"), lit on the controller; NONE */
	short button;
	boolean challenge;
	char const *words;
	char const *done;
};

/* what the player did since the step began (basic_training_tick) */
struct basic_training_seen
{
	/* in the headset (the steps of the hands are not asked out of it) */
	boolean vr;
	real turned;
	real looked_up;
	real moved;
	short spots;
	short rounds_fired;
	short reloads;
	short switches;
	boolean gripped;
	boolean fitted;
	short targets_down;
	boolean magazine_dry;
	boolean sniper_held;
	boolean pistol_held;
	boolean sniper_hit;
	boolean pistol_hit;
	short grenades_thrown;
	short melee_hits;
	short grenade_hits;
	/* the player's side's kills, and the other side's */
	short kills;
	short enemy_kills;
};

/* ---------- the chapters and their steps */

static struct basic_training_chapter const basic_training_chapters[BASIC_TRAINING_CHAPTERS] =
{
	{ "FIT CHECK", "Learn: turning, the comfort vignette, seated or standing, your height.\n"
		"Ends with: the settings that feel right.", 12.0f, 0.0f, -90.0f, _training_loadout_rifle, 0 },
	{ "BOOTS ON THE GROUND", "Learn: look, move, turn.\nEnds with: a run to three marked spots.",
		12.0f, 0.0f, -90.0f, _training_loadout_rifle, 0 },
	{ "WEAPONS HOT", "Learn: aim, fire, reload.\nEnds with: three targets down on one magazine.",
		12.0f, 0.0f, -90.0f, _training_loadout_rifle, 0 },
	{ "LOADOUT", "Learn: hold the foregrip, switch weapons, pick up and swap.\n"
		"Ends with: a hit with the sniper rifle and one with the pistol.", 12.0f, 0.0f, -90.0f,
		_training_loadout_rifle_plasma, 0 },
	{ "HANDS ON", "Learn: throw a grenade, melee.\nEnds with: a grenade on a group, then a blow.",
		13.0f, -4.0f, -90.0f, _training_loadout_rifle, 2 },
	{ "EYES UP", "Learn: the scope.\nEnds with: a sniper duel from the blue base's roof.\nComing soon.",
		12.0f, 0.0f, -90.0f, _training_loadout_rifle, 0 },
	{ "MOUNT UP", "Learn: vehicles.\nEnds with: the Warthog's gun, a bot driving.\nComing soon.",
		12.0f, 0.0f, -90.0f, _training_loadout_rifle, 0 },
	{ "LIVE FIRE", "Learn: all of it.\nEnds with: one on one, then two on two with a bot on your side.",
		12.0f, 0.0f, -90.0f, _training_loadout_rifle_pistol, 2 },
};

static struct basic_training_step const basic_training_steps[] =
{
	{ 0, _training_check_turn, 90, _training_setup_clear, _training_button_right_stick, FALSE,
		"Welcome to the VR Tutorial, recruit. Turn around with the right stick.",
		"Good. You can turn. We're making progress." },
	{ 0, _training_check_fit, 0, _training_setup_clear, NONE, TRUE,
		"Fit check. Pick what feels right: snap or smooth turns, the comfort vignette, seated or standing, "
		"your height.",
		"Squared away. Change it any time in VR Setup." },
	{ 1, _training_check_look_up, 25, _training_setup_clear, NONE, FALSE,
		"Eyes up, Spartan. Look up at the canyon rim.",
		"Nice view. Don't get used to it." },
	{ 1, _training_check_move, 4, _training_setup_clear, _training_button_left_stick, FALSE,
		"Move out with the left stick.",
		"Those legs work. Outstanding." },
	{ 1, _training_check_spots, TRAINING_SPOTS, _training_setup_spots, NONE, TRUE,
		"Challenge: run to the three marked spots. Move it, move it!",
		"All three. Not bad for a rookie." },
	{ 2, _training_check_fire, 6, _training_setup_range, _training_button_right_trigger, FALSE,
		"Point the rifle downrange and pull %s to fire.",
		"Weapons hot. Try to hit something next time." },
	{ 2, _training_check_reload, 1, _training_setup_range, _training_button_x, FALSE,
		"Reload: press %s, or take a magazine from your left hip.",
		"Locked and loaded." },
	{ 2, _training_check_targets, 3, _training_setup_targets, NONE, TRUE,
		"Challenge: three targets downrange. Drop all three before that magazine runs dry.",
		"Three for three. Now that's shooting." },
	{ 3, _training_check_grip, 1, _training_setup_range, _training_button_left_grip, FALSE,
		"Steady the rifle: put your left hand on its foregrip and squeeze %s.",
		"Two hands, steady aim. That's how it's done." },
	{ 3, _training_check_switch, 1, _training_setup_range, _training_button_y, FALSE,
		"Switch weapons with %s. Or reach down to your hip for the sidearm.",
		"Quick hands. Keep it up." },
	{ 3, _training_check_loadout, 2, _training_setup_loadout, _training_button_x, TRUE,
		"Challenge: a sniper rifle and a pistol lie ahead. Hold %s over each to swap, then hit the target "
		"with both.",
		"Sniper and sidearm. You're getting dangerous." },
	{ 4, _training_check_grenade, 1, _training_setup_range, _training_button_left_trigger, FALSE,
		"Grenade: hold %s, draw your hand back, swing and let go.",
		"Fire in the hole! Good arm." },
	{ 4, _training_check_melee, 1, _training_setup_dummy, _training_button_b, FALSE,
		"Melee: swing the rifle hard, or press %s. Hit the dummy.",
		"Ouch. He felt that." },
	{ 4, _training_check_group, 2, _training_setup_group, _training_button_left_trigger, TRUE,
		"Challenge: three of them behind that cover. A grenade onto the group, then finish one up close.",
		"Grenade, then a beat-down. Textbook." },
	{ 7, _training_check_score, 2, _training_setup_duel, NONE, TRUE,
		"Live fire. One on one, and he shoots back. First to two kills.",
		"He's down. Now let's make it a team." },
	{ 7, _training_check_score, 5, _training_setup_team, NONE, TRUE,
		"Two on two. You've got a bot on your side. First team to five.",
		"Tutorial complete. You'll do, Spartan." },
};

#define TRAINING_STEP_COUNT ((short)NUMBEROF(basic_training_steps))

/* the marked spots of chapter 1 (the field's frame), and the targets' and
others' posts */
static real const training_spots[TRAINING_SPOTS][2] = { { 7.0f, -9.0f }, { 2.0f, -16.0f }, { 11.0f, -21.0f } };

/* ---------- globals */

enum
{
	_training_off = 0,
	/* the game being made (from the main menu) */
	_training_starting,
	/* the game played */
	_training_playing,
};

static struct
{
	short state;
	short start_chapter;
	short start_step;
	real start_seconds;
	short start_stage;
	/* debug.training read */
	boolean debug_checked;
	real menu_seconds;

	/* the chapter played, its step; a chapter or step asked for (set up at
	the next tick), NONE */
	short chapter;
	short step;
	short pending_chapter;
	short pending_step;
	/* the step's: when it began (game ticks), what was seen since */
	long step_tick;
	struct basic_training_seen seen;
	/* the chapter done or the challenge failed: the next chapter, or the
	step set up again, at this game tick */
	long next_tick;
	boolean chapter_done;
	boolean all_done;
	/* the HUD's line of a step done (or failed), and when */
	char feedback[128];
	long feedback_tick;
	boolean feedback_failed;
	/* the chapters finished (a bit each) */
	unsigned long finished;
	boolean finished_read;

	/* the field: the red flag, the way up the canyon (to the blue flag)
	and its left */
	boolean field_known;
	real_point3d field_origin;
	real_vector2d field_forward;
	real_vector2d field_left;

	/* the spots reached, the weapons placed */
	boolean spot_reached[TRAINING_SPOTS];
	long placed_weapons[2];
	short nav_index;
	boolean nav_looked;

	/* the trainee's state last tick */
	long unit_index;
	real yaw;
	real_point3d position;
	long weapon_index;
	short rounds_loaded;
	long fired_time;
	short grenades;
	long fire_tick;
	short fire_family;
	long throw_tick;
	long melee_tick;
	short kills_base;
	short enemy_kills_base;
	/* each bot's: its unit, its health, its deaths at the step's start,
	its kills then; hit by a grenade, by a blow, in this step */
	long bot_unit[TRAINING_BOTS];
	real bot_vitality[TRAINING_BOTS];
	short bot_deaths[TRAINING_BOTS];
	short bot_kills[TRAINING_BOTS];
	boolean bot_grenaded[TRAINING_BOTS];
	boolean bot_struck[TRAINING_BOTS];
	/* vr_hands.c's foregrip */
	real two_handed;
	/* the fit check's screen opened (its step), or tried */
	boolean fit_opened;
	boolean fit_tried;
	/* the D-pad's left and right, last frame */
	boolean dpad_left;
	boolean dpad_right;
} basic_training =
{
	_training_off, NONE, 0, 0.0f, 0, FALSE, 0.0f, NONE, 0, NONE, 0,
};

/* ---------- the steps' logic (the harness's: tools/harness/tests/test_basic_training.py) */

/* a step's result from what was seen since it began */
static short basic_training_step_result(
	struct basic_training_step const *step,
	struct basic_training_seen const *seen)
{
	switch (step->check)
	{
	case _training_check_turn:
		return seen->turned >= step->amount ? _training_result_done : _training_result_waiting;
	case _training_check_look_up:
		return seen->looked_up >= step->amount ? _training_result_done : _training_result_waiting;
	case _training_check_move:
		return seen->moved >= step->amount ? _training_result_done : _training_result_waiting;
	case _training_check_fit:
		return seen->fitted || !seen->vr ? _training_result_done : _training_result_waiting;
	case _training_check_spots:
		return seen->spots >= step->amount ? _training_result_done : _training_result_waiting;
	case _training_check_fire:
		return seen->rounds_fired >= step->amount ? _training_result_done : _training_result_waiting;
	case _training_check_reload:
		return seen->reloads >= step->amount ? _training_result_done : _training_result_waiting;
	case _training_check_switch:
		return seen->switches >= step->amount ? _training_result_done : _training_result_waiting;
	case _training_check_grip:
		return seen->gripped || !seen->vr ? _training_result_done : _training_result_waiting;
	case _training_check_targets:
		/* (the last target down with the last round counts) */
		if (seen->targets_down >= step->amount)
			return _training_result_done;
		return seen->magazine_dry ? _training_result_failed : _training_result_waiting;
	case _training_check_loadout:
		return seen->sniper_held && seen->pistol_held && seen->sniper_hit && seen->pistol_hit ?
			_training_result_done : _training_result_waiting;
	case _training_check_grenade:
		return seen->grenades_thrown >= step->amount ? _training_result_done : _training_result_waiting;
	case _training_check_melee:
		return seen->melee_hits >= step->amount ? _training_result_done : _training_result_waiting;
	case _training_check_group:
		return seen->grenade_hits >= step->amount && seen->melee_hits > 0 ?
			_training_result_done : _training_result_waiting;
	case _training_check_score:
		if (seen->kills >= step->amount)
			return _training_result_done;
		return seen->enemy_kills >= step->amount ? _training_result_failed : _training_result_waiting;
	}
	return _training_result_waiting;
}

boolean basic_training_chapter_ready(
	short chapter)
{
	return chapter >= 0 && chapter < BASIC_TRAINING_CHAPTERS && chapter != TRAINING_CHAPTER_SNIPER &&
		chapter != TRAINING_CHAPTER_WARTHOG;
}

/* the chapter after (direction 1) or before (-1) one, past those not made
yet; NONE at either end */
static short basic_training_next_chapter(
	short chapter,
	short direction)
{
	short next = chapter;

	do
	{
		next = (short)(next + direction);
	}
	while (next >= 0 && next < BASIC_TRAINING_CHAPTERS && !basic_training_chapter_ready(next));
	return next >= 0 && next < BASIC_TRAINING_CHAPTERS ? next : NONE;
}

/* a chapter's first step, and its count of them; NONE, 0 for one with none */
static short basic_training_first_step(
	short chapter,
	short *count)
{
	short first = NONE;
	short index;

	*count = 0;
	for (index = 0; index < TRAINING_STEP_COUNT; index++)
	{
		if (basic_training_steps[index].chapter != chapter)
			continue;
		if (first == NONE)
			first = index;
		(*count)++;
	}
	return first;
}

/* the step a chapter jumped to begins at: its step_in_chapter-th (0 its
first), within it; NONE for a chapter with no steps (not made yet). Nothing
of the chapters before it is asked: each sets itself up */
static short basic_training_jump_step(
	short chapter,
	short step_in_chapter)
{
	short count;
	short first = basic_training_first_step(chapter, &count);

	if (first == NONE)
		return NONE;
	return (short)(first + PIN(step_in_chapter, 0, count - 1));
}

/* vr.training_finished's chapters ("0,1,4"), a bit each; and back */
static unsigned long basic_training_finished_parse(
	char const *text)
{
	unsigned long finished = 0;

	while (text && *text)
	{
		if (*text >= '0' && *text <= '9')
		{
			long chapter = strtol(text, (char **)&text, 10);

			if (chapter >= 0 && chapter < BASIC_TRAINING_CHAPTERS)
				finished |= 1UL << chapter;
		}
		else
			text++;
	}
	return finished;
}

static void basic_training_finished_text(
	unsigned long finished,
	char *text,
	size_t size)
{
	short chapter;
	size_t used = 0;

	if (!size)
		return;
	text[0] = 0;
	for (chapter = 0; chapter < BASIC_TRAINING_CHAPTERS; chapter++)
	{
		if ((finished & (1UL << chapter)) && used + 3 < size)
			used += (size_t)snprintf(text + used, size - used, "%s%d", used ? "," : "", chapter);
	}
}

/* the step's words with the button's name for "%s" */
static void basic_training_words(
	char const *words,
	char const *button,
	char *text,
	size_t size)
{
	char const *mark = strstr(words, "%s");

	if (!mark)
		snprintf(text, size, "%s", words);
	else
		snprintf(text, size, "%.*s%s%s", (int)(mark - words), words, button, mark + 2);
}

/* ---------- the field */

/* Blood Gulch's frame: the red flag, up the canyon to the blue flag, and
its left (the scenario's flags) */
static boolean basic_training_field(
	void)
{
	struct scenario *scenario = global_scenario_get();
	boolean found[2] = { FALSE, FALSE };
	real_point3d flags[2];
	long index;
	real length;

	if (basic_training.field_known)
		return TRUE;
	if (!scenario)
		return FALSE;
	for (index = 0; index < scenario->netgame_flags.count; index++)
	{
		struct scenario_netgame_flag *flag =
			TAG_BLOCK_GET_ELEMENT(&scenario->netgame_flags, index, struct scenario_netgame_flag);

		if (flag->type == _netgame_flag_ctf_flag && (flag->team_index == 0 || flag->team_index == 1) &&
			!found[flag->team_index])
		{
			found[flag->team_index] = TRUE;
			flags[flag->team_index] = flag->position;
		}
	}
	if (!found[0] || !found[1])
		return FALSE;
	basic_training.field_origin = flags[0];
	basic_training.field_forward.i = flags[1].x - flags[0].x;
	basic_training.field_forward.j = flags[1].y - flags[0].y;
	length = (real)sqrt(basic_training.field_forward.i * basic_training.field_forward.i +
		basic_training.field_forward.j * basic_training.field_forward.j);
	if (length < 1.0f)
		return FALSE;
	basic_training.field_forward.i /= length;
	basic_training.field_forward.j /= length;
	basic_training.field_left.i = -basic_training.field_forward.j;
	basic_training.field_left.j = basic_training.field_forward.i;
	basic_training.field_known = TRUE;
	platform_log("training: the field from the red flag (%.2f, %.2f, %.2f) to the blue (%.2f, %.2f, %.2f)",
		flags[0].x, flags[0].y, flags[0].z, flags[1].x, flags[1].y, flags[1].z);
	return TRUE;
}

/* a place of the field (along the canyon, left of that) on the ground */
static void basic_training_field_point(
	real along,
	real left,
	real_point3d *point)
{
	struct collision_result collision;
	real_point3d start;
	real_vector3d down = { 0.0f, 0.0f, -40.0f };

	point->x = basic_training.field_origin.x + basic_training.field_forward.i * along +
		basic_training.field_left.i * left;
	point->y = basic_training.field_origin.y + basic_training.field_forward.j * along +
		basic_training.field_left.j * left;
	point->z = basic_training.field_origin.z;
	start = *point;
	start.z += 20.0f;
	if (collision_test_vector(FLAG(_collision_test_structure_bit) | FLAG(_collision_test_front_facing_surfaces_bit),
		&start, &down, NONE, &collision) && collision.type == _collision_result_structure)
	{
		*point = collision.point;
	}
	point->z += 0.1f;
}

/* the yaw (radians) a degrees left of up the canyon */
static real basic_training_field_yaw(
	real degrees)
{
	return (real)atan2(basic_training.field_forward.j, basic_training.field_forward.i) + degrees * TRAINING_PI / 180.0f;
}

/* ---------- the trainee */

static long basic_training_trainee(
	void)
{
	long player_index = local_player_get_player_index(0);

	return player_index;
}

static struct unit_datum *basic_training_trainee_unit(
	long *unit_index)
{
	long player_index = basic_training_trainee();
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	struct unit_datum *unit;

	*unit_index = NONE;
	if (!player || player->unit_index == NONE)
		return NULL;
	unit = unit_try_and_get(player->unit_index);
	if (!unit || TEST_FLAG(unit->object.damage_flags, _object_dead_bit))
		return NULL;
	*unit_index = player->unit_index;
	return unit;
}

static long basic_training_weapon_tag(
	char const *name)
{
	struct tag_iterator iterator;
	long tag_index;

	tag_iterator_new(&iterator, WEAPON_DEFINITION_TAG);
	while ((tag_index = tag_iterator_next(&iterator)) != NONE)
	{
		char const *tag_name = tag_get_name(tag_index);

		if (tag_name && !strcmp(tag_name, name))
			return tag_index;
	}
	return NONE;
}

/* the weapon's kind: 1 the sniper rifle, 2 the pistol, 0 another */
static short basic_training_weapon_family(
	long weapon_index)
{
	struct weapon_datum *weapon = weapon_index != NONE ? weapon_try_and_get(weapon_index) : NULL;
	char const *name = weapon ? tag_get_name(weapon->definition_index) : NULL;

	if (!name)
		return 0;
	if (!strcmp(name, "weapons\\sniper rifle\\sniper rifle"))
		return 1;
	if (!strcmp(name, "weapons\\pistol\\pistol"))
		return 2;
	return 0;
}

static long basic_training_current_weapon(
	struct unit_datum *unit)
{
	short slot = unit->unit.current_weapon_index;

	return slot >= 0 && slot < MAXIMUM_WEAPONS_PER_UNIT ? unit->unit.weapon_object_indices[slot] : NONE;
}

/* the unit's weapons all gone (as gun_game.c's gun_game_delete_weapons),
and those named given, the first in its hands */
static void basic_training_arm(
	long unit_index,
	short loadout,
	short grenades)
{
	static char const *const loadouts[NUMBER_OF_TRAINING_LOADOUTS][2] =
	{
		{ "weapons\\assault rifle\\assault rifle", NULL },
		{ "weapons\\assault rifle\\assault rifle", "weapons\\plasma pistol\\plasma pistol" },
		{ "weapons\\assault rifle\\assault rifle", "weapons\\pistol\\pistol" },
	};
	struct unit_datum *unit = unit_get(unit_index);
	short slot, other, index;

	for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
	{
		long weapon_index = unit->unit.weapon_object_indices[slot];

		if (weapon_index == NONE)
			continue;
		for (other = slot; other < MAXIMUM_WEAPONS_PER_UNIT; other++)
		{
			if (unit->unit.weapon_object_indices[other] == weapon_index)
			{
				unit->unit.weapon_object_indices[other] = NONE;
				if (unit->unit.current_weapon_index == other)
					unit->unit.current_weapon_index = NONE;
				if (unit->unit.desired_weapon_index == other)
					unit->unit.desired_weapon_index = NONE;
			}
		}
		if (object_try_and_get(weapon_index))
			object_delete(weapon_index);
	}
	/* (the second first, so that the first is in the hands) */
	for (index = 1; index >= 0; index--)
	{
		char const *name = loadouts[PIN(loadout, 0, NUMBER_OF_TRAINING_LOADOUTS - 1)][index];
		long tag_index = name ? basic_training_weapon_tag(name) : NONE;
		struct object_placement_data placement;
		long weapon_index;

		if (tag_index == NONE)
		{
			if (name)
				platform_log("training: no %s on this map", name);
			continue;
		}
		object_placement_data_new(&placement, tag_index, NONE);
		weapon_index = object_new(&placement);
		/* (as a starting weapon: replacing would take the other) */
		if (weapon_index != NONE && !unit_add_weapon_to_inventory(unit_index, weapon_index, _unit_add_weapon_starting))
		{
			platform_log("training: %s not given", name);
			object_delete(weapon_index);
		}
	}
	unit = unit_get(unit_index);
	unit->unit.grenade_counts[0] = (char)grenades;
	unit->unit.grenade_counts[1] = 0;
	unit->unit.current_grenade_index = 0;
}

/* the trainee put at the chapter's place, turned up the canyon, with its
weapons */
static void basic_training_place_trainee(
	long unit_index)
{
	struct basic_training_chapter const *chapter = &basic_training_chapters[basic_training.chapter];
	real_point3d point;
	real_vector3d forward;
	real yaw;

	basic_training_field_point(chapter->along, chapter->left, &point);
	yaw = basic_training_field_yaw(chapter->facing);
	forward.i = (real)cos(yaw);
	forward.j = (real)sin(yaw);
	forward.k = 0.0f;
	object_set_position(unit_index, &point, &forward, NULL);
	{
		struct unit_datum *unit = unit_get(unit_index);

		unit->unit.desired_facing_vector = forward;
		unit->unit.desired_aiming_vector = forward;
		unit->unit.desired_looking_vector = forward;
	}
	player_control_set_facing(0, &forward);
	basic_training_arm(unit_index, chapter->loadout, chapter->grenades);
}

/* ---------- the bots */

static void basic_training_order(
	long bot_index,
	short kind,
	short skill,
	real along,
	real left,
	real other_along,
	real other_left,
	long period_ticks)
{
	struct bots_order order;

	csmemset(&order, 0, sizeof(order));
	order.kind = kind;
	order.skill = skill;
	order.has_post = kind != _bots_order_park;
	if (order.has_post)
	{
		basic_training_field_point(along, left, &order.post);
		basic_training_field_point(other_along, other_left, &order.other);
	}
	order.period_ticks = period_ticks;
	bots_set_order(bot_index, &order);
}

static void basic_training_park_all(
	void)
{
	long bot_index;

	for (bot_index = 0; bot_index < TRAINING_BOTS; bot_index++)
		basic_training_order(bot_index, _bots_order_park, NONE, 0.0f, 0.0f, 0.0f, 0.0f, 0);
}

static void basic_training_delete_placed(
	void)
{
	short index;

	for (index = 0; index < (short)NUMBEROF(basic_training.placed_weapons); index++)
	{
		long weapon_index = basic_training.placed_weapons[index];
		struct weapon_datum *weapon = weapon_index != NONE ? weapon_try_and_get(weapon_index) : NULL;

		/* (not one the trainee picked up) */
		if (weapon && weapon->object.parent_object_index == NONE)
			object_delete(weapon_index);
		basic_training.placed_weapons[index] = NONE;
	}
}

static long basic_training_place_weapon(
	char const *name,
	real along,
	real left)
{
	long tag_index = basic_training_weapon_tag(name);
	struct object_placement_data placement;

	if (tag_index == NONE)
		return NONE;
	object_placement_data_new(&placement, tag_index, NONE);
	basic_training_field_point(along, left, &placement.position);
	placement.position.z += 0.2f;
	return object_new(&placement);
}

/* ---------- steps */

static void basic_training_message(
	char const *text,
	boolean failed)
{
	snprintf(basic_training.feedback, sizeof(basic_training.feedback), "%s", text);
	basic_training.feedback_tick = game_time_get();
	basic_training.feedback_failed = failed;
	platform_log("training: %s", text);
}

/* what is seen from now on is the step's: the trainee's and the bots'
counts taken as they are */
static void basic_training_seen_reset(
	void)
{
	long bot_index;
	long player_index = basic_training_trainee();
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;

	csmemset(&basic_training.seen, 0, sizeof(basic_training.seen));
	csmemset(basic_training.spot_reached, 0, sizeof(basic_training.spot_reached));
	basic_training.step_tick = game_time_get();
	basic_training.kills_base = player ? player->statistics.kills[0] : 0;
	basic_training.enemy_kills_base = 0;
	basic_training.fit_opened = FALSE;
	basic_training.fit_tried = FALSE;
	/* (the trainee's state taken again at the next tick: weapons given by
	the set up are no switch, nor its full magazine a reload) */
	basic_training.unit_index = NONE;
	basic_training.fire_tick = basic_training.throw_tick = basic_training.melee_tick = -TICKS_PER_SECOND * 60;
	for (bot_index = 0; bot_index < TRAINING_BOTS; bot_index++)
	{
		long bot_player_index = bots_player_of_bot(bot_index);
		struct player_datum *bot = bot_player_index != NONE ? player_try_and_get(bot_player_index) : NULL;

		basic_training.bot_deaths[bot_index] = bot ? bot->statistics.deaths : 0;
		basic_training.bot_kills[bot_index] = bot ? bot->statistics.kills[0] : 0;
		basic_training.bot_grenaded[bot_index] = FALSE;
		basic_training.bot_struck[bot_index] = FALSE;
	}
}

/* the step's set up: its bots' orders, what it places */
static void basic_training_setup(
	short setup)
{
	long unit_index;
	struct unit_datum *unit;

	basic_training_delete_placed();
	switch (setup)
	{
	case _training_setup_clear:
	case _training_setup_spots:
		basic_training_park_all();
		break;
	case _training_setup_range:
		basic_training_park_all();
		basic_training_order(0, _bots_order_hold, NONE, 7.0f, -16.0f, 12.0f, 0.0f, 0);
		break;
	case _training_setup_targets:
		basic_training_order(0, _bots_order_hold, NONE, 12.0f, -12.5f, 12.0f, 0.0f, 0);
		basic_training_order(1, _bots_order_hold, NONE, 8.5f, -14.5f, 12.0f, 0.0f, 0);
		basic_training_order(2, _bots_order_hold, NONE, 5.5f, -11.5f, 12.0f, 0.0f, 0);
		basic_training_order(TRAINING_TEAMMATE, _bots_order_park, NONE, 0.0f, 0.0f, 0.0f, 0.0f, 0);
		/* (one full magazine, and no more) */
		unit = basic_training_trainee_unit(&unit_index);
		if (unit)
		{
			long weapon_index = basic_training_current_weapon(unit);
			struct weapon_datum *weapon = weapon_index != NONE ? weapon_try_and_get(weapon_index) : NULL;

			if (weapon)
			{
				struct weapon_definition const *definition =
					tag_get(WEAPON_DEFINITION_TAG, weapon->definition_index);

				if (definition->weapon.magazines.count > 0)
				{
					struct weapon_magazine_definition const *magazine =
						TAG_BLOCK_GET_ELEMENT(&definition->weapon.magazines, 0, struct weapon_magazine_definition);

					weapon->weapon.magazines[0].rounds_loaded = magazine->rounds_loaded_maximum;
					weapon->weapon.magazines[0].rounds_total = 0;
				}
			}
		}
		break;
	case _training_setup_loadout:
		basic_training_park_all();
		basic_training_order(0, _bots_order_strafe, NONE, 5.0f, -15.0f, 10.0f, -16.0f, TICKS_PER_SECOND * 3);
		basic_training.placed_weapons[0] = basic_training_place_weapon("weapons\\sniper rifle\\sniper rifle",
			13.0f, -3.0f);
		basic_training.placed_weapons[1] = basic_training_place_weapon("weapons\\pistol\\pistol", 10.5f, -3.0f);
		break;
	case _training_setup_dummy:
		basic_training_park_all();
		basic_training_order(0, _bots_order_hold, NONE, 13.0f, -6.5f, 13.0f, -4.0f, 0);
		break;
	case _training_setup_group:
		basic_training_order(0, _bots_order_peek, NONE, 15.8f, -15.1f, 13.9f, -15.7f, TICKS_PER_SECOND * 2);
		basic_training_order(1, _bots_order_hold, NONE, 16.3f, -14.45f, 13.0f, -4.0f, 0);
		basic_training_order(2, _bots_order_hold, NONE, 16.9f, -15.2f, 13.0f, -4.0f, 0);
		basic_training_order(TRAINING_TEAMMATE, _bots_order_park, NONE, 0.0f, 0.0f, 0.0f, 0.0f, 0);
		unit = basic_training_trainee_unit(&unit_index);
		if (unit)
			unit->unit.grenade_counts[0] = 4;
		break;
	case _training_setup_duel:
		basic_training_park_all();
		basic_training_order(0, _bots_order_free, 0 /* easy */, 4.0f, -24.0f, 12.0f, 0.0f, 0);
		break;
	case _training_setup_team:
		basic_training_order(0, _bots_order_free, 0 /* easy */, 2.0f, -24.0f, 12.0f, 0.0f, 0);
		basic_training_order(1, _bots_order_free, 0 /* easy */, 7.0f, -25.0f, 12.0f, 0.0f, 0);
		basic_training_order(2, _bots_order_park, NONE, 0.0f, 0.0f, 0.0f, 0.0f, 0);
		basic_training_order(TRAINING_TEAMMATE, _bots_order_free, 0 /* easy */, 13.0f, 1.0f, 4.0f, -24.0f, 0);
		break;
	}
}

static short basic_training_step_button(struct basic_training_step const *step);

/* the step begins: its set up, what is seen from now; the button it names
buzzed in its hand */
static void basic_training_step_begin(
	void)
{
	struct basic_training_step const *step = &basic_training_steps[basic_training.step];

	basic_training_setup(step->setup);
	basic_training_seen_reset();
	basic_training.next_tick = NONE;
	if (step->challenge)
		game_engine_play_multiplayer_sound(_training_sound_countdown_timer);
#ifdef HALO_VR
	if (step->button != NONE && halo_vr_running())
	{
		short button = basic_training_step_button(step);
		boolean left_hand = button == _training_button_left_bumper ||
			button == _training_button_left_trigger || button == _training_button_left_stick ||
			button == _training_button_left_grip;

		if (halo_vr_left_handed())
			left_hand = !left_hand;
		halo_vr_rumble(left_hand ? 0.35f : 0.0f, left_hand ? 0.0f : 0.35f);
	}
#endif
	platform_log("training: chapter %d, step %d", basic_training.chapter, basic_training.step);
}

/* a chapter begins at a step of it (its first: 0): the trainee at its
place, with its weapons */
static void basic_training_chapter_begin(
	short chapter,
	short step_in_chapter)
{
	short step = basic_training_jump_step(chapter, step_in_chapter);
	long unit_index;

	if (step == NONE)
		return;
	basic_training.chapter = chapter;
	basic_training.step = step;
	basic_training.chapter_done = FALSE;
	basic_training.all_done = FALSE;
	basic_training.feedback[0] = 0;
	if (basic_training_trainee_unit(&unit_index))
		basic_training_place_trainee(unit_index);
	game_engine_play_multiplayer_sound(chapter == 7 ? _training_sound_team_slayer : _training_sound_slayer);
	basic_training_step_begin();
}

static void basic_training_finish_chapter(
	void)
{
	char text[64];

	basic_training.finished |= 1UL << basic_training.chapter;
	basic_training_finished_text(basic_training.finished, text, sizeof(text));
	config_write("vr.training_finished", text);
	basic_training.chapter_done = TRUE;
	basic_training.next_tick = game_time_get() + (long)(TRAINING_NEXT_CHAPTER_SECONDS * TICKS_PER_SECOND);
	game_engine_play_multiplayer_sound(_training_sound_countdown_timer_end);
	if (basic_training_next_chapter(basic_training.chapter, 1) == NONE)
		basic_training.all_done = TRUE;
}

/* ---------- what is seen */

static real basic_training_angle(
	real angle)
{
	while (angle > TRAINING_PI)
		angle -= 2.0f * TRAINING_PI;
	while (angle < -TRAINING_PI)
		angle += 2.0f * TRAINING_PI;
	return angle;
}

/* the trainee's unit and the bots, against last tick */
static void basic_training_watch(
	void)
{
	struct basic_training_seen *seen = &basic_training.seen;
	long now = game_time_get();
	long unit_index;
	struct unit_datum *unit = basic_training_trainee_unit(&unit_index);
	long player_index = basic_training_trainee();
	struct player_datum *player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	long bot_index;
	short team = player ? (short)player->team_index : 0;

#ifdef HALO_VR
	seen->vr = halo_vr_running() != 0;
#else
	seen->vr = FALSE;
#endif
	if (unit)
	{
		real_vector3d facing = unit->unit.desired_facing_vector;
		real yaw = (real)atan2(facing.j, facing.i);
		real pitch = (real)asin(PIN(unit->unit.looking_vector.k, -1.0f, 1.0f)) * 180.0f / TRAINING_PI;
		real_point3d position = unit->object.position;
		long weapon_index = basic_training_current_weapon(unit);
		struct weapon_datum *weapon = weapon_index != NONE ? weapon_try_and_get(weapon_index) : NULL;
		short grenades = (short)(unit->unit.grenade_counts[0] + unit->unit.grenade_counts[1]);
		short slot;

		if (basic_training.unit_index == unit_index)
		{
			real step = (real)sqrt((position.x - basic_training.position.x) * (position.x - basic_training.position.x) +
				(position.y - basic_training.position.y) * (position.y - basic_training.position.y));

			seen->turned += (real)fabs(basic_training_angle(yaw - basic_training.yaw)) * 180.0f / TRAINING_PI;
			/* (not a teleport) */
			if (step < 1.0f)
				seen->moved += step;
			if (weapon_index != basic_training.weapon_index && basic_training.weapon_index != NONE &&
				weapon_index != NONE)
			{
				seen->switches++;
			}
			else if (weapon && weapon_index == basic_training.weapon_index)
			{
				short loaded = weapon->weapon.magazines[0].rounds_loaded;

				if (loaded < basic_training.rounds_loaded)
					seen->rounds_fired = (short)(seen->rounds_fired + basic_training.rounds_loaded - loaded);
				else if (loaded > basic_training.rounds_loaded)
					seen->reloads++;
			}
			if (weapon && weapon->weapon.game_time_last_fired != basic_training.fired_time)
			{
				basic_training.fire_tick = now;
				basic_training.fire_family = basic_training_weapon_family(weapon_index);
			}
			if (grenades < basic_training.grenades)
			{
				seen->grenades_thrown = (short)(seen->grenades_thrown + basic_training.grenades - grenades);
				basic_training.throw_tick = now;
			}
		}
#ifdef HALO_VR
		/* (in the headset the aim is the controller's: the head looking up
		counts too) */
		{
			float head_forward[3], head_up[3], head_position[3];

			if (seen->vr && halo_vr_head(head_forward, head_up, head_position))
			{
				real head_pitch = (real)asin(PIN(head_forward[2], -1.0f, 1.0f)) * 180.0f / TRAINING_PI;

				pitch = MAX(pitch, head_pitch);
			}
		}
#endif
		if (pitch > seen->looked_up)
			seen->looked_up = pitch;
		if (TEST_FLAG(unit->unit.control_flags, _unit_control_use_equipment_bit) || unit->unit.melee_attack_state)
			basic_training.melee_tick = now;
		basic_training.unit_index = unit_index;
		basic_training.yaw = yaw;
		basic_training.position = position;
		basic_training.weapon_index = weapon_index;
		basic_training.rounds_loaded = weapon ? weapon->weapon.magazines[0].rounds_loaded : 0;
		basic_training.fired_time = weapon ? weapon->weapon.game_time_last_fired : 0;
		basic_training.grenades = grenades;
		/* (the magazine dry: the weapon in the hands with none loaded) */
		seen->magazine_dry = weapon && weapon->weapon.magazines[0].rounds_loaded <= 0 &&
			weapon->weapon.magazines[0].rounds_total <= 0;
		for (slot = 0; slot < MAXIMUM_WEAPONS_PER_UNIT; slot++)
		{
			short family = basic_training_weapon_family(unit->unit.weapon_object_indices[slot]);

			if (family == 1)
				seen->sniper_held = TRUE;
			else if (family == 2)
				seen->pistol_held = TRUE;
		}
		/* the spots: reached as the trainee comes near */
		if (basic_training_steps[basic_training.step].check == _training_check_spots)
		{
			short spot;

			for (spot = 0; spot < TRAINING_SPOTS; spot++)
			{
				real_point3d point;

				basic_training_field_point(training_spots[spot][0], training_spots[spot][1], &point);
				if (!basic_training.spot_reached[spot] &&
					(point.x - position.x) * (point.x - position.x) + (point.y - position.y) * (point.y - position.y) <
					TRAINING_SPOT_REACH * TRAINING_SPOT_REACH)
				{
					basic_training.spot_reached[spot] = TRUE;
					seen->spots++;
					game_engine_play_multiplayer_sound(_training_sound_countdown_timer);
				}
			}
		}
	}
	else
		basic_training.unit_index = NONE;
	seen->gripped = seen->gripped || basic_training.two_handed > 0.8f;

	/* the bots: hurt (by what, as near as can be told), down, scoring */
	seen->targets_down = 0;
	seen->enemy_kills = 0;
	seen->kills = player ? (short)(player->statistics.kills[0] - basic_training.kills_base) : 0;
	for (bot_index = 0; bot_index < TRAINING_BOTS; bot_index++)
	{
		long bot_player_index = bots_player_of_bot(bot_index);
		struct player_datum *bot = bot_player_index != NONE ? player_try_and_get(bot_player_index) : NULL;
		struct unit_datum *bot_unit = bot && bot->unit_index != NONE ? unit_try_and_get(bot->unit_index) : NULL;
		real vitality = bot_unit ? bot_unit->object.body_vitality + bot_unit->object.shield_vitality : 0.0f;

		if (!bot)
			continue;
		if (bot->statistics.deaths > basic_training.bot_deaths[bot_index] &&
			bots_order_kind(bot_index) != _bots_order_park)
		{
			seen->targets_down++;
		}
		if (bot->team_index == team)
			seen->kills = (short)(seen->kills + bot->statistics.kills[0] - basic_training.bot_kills[bot_index]);
		else
			seen->enemy_kills = (short)(seen->enemy_kills + bot->statistics.kills[0] - basic_training.bot_kills[bot_index]);
		if (bot_unit && basic_training.bot_unit[bot_index] == bot->unit_index &&
			vitality < basic_training.bot_vitality[bot_index] - 0.01f)
		{
			real distance = unit ? (real)sqrt(
				(bot_unit->object.position.x - basic_training.position.x) *
				(bot_unit->object.position.x - basic_training.position.x) +
				(bot_unit->object.position.y - basic_training.position.y) *
				(bot_unit->object.position.y - basic_training.position.y)) : 100.0f;

			if (now - basic_training.melee_tick <= TRAINING_MELEE_TICKS && distance < 3.0f)
			{
				if (!basic_training.bot_struck[bot_index])
					seen->melee_hits++;
				basic_training.bot_struck[bot_index] = TRUE;
			}
			else if (now - basic_training.fire_tick <= TRAINING_SHOT_TICKS)
			{
				if (basic_training.fire_family == 1)
					seen->sniper_hit = TRUE;
				else if (basic_training.fire_family == 2)
					seen->pistol_hit = TRUE;
			}
			else if (now - basic_training.throw_tick <= TRAINING_GRENADE_TICKS)
			{
				if (!basic_training.bot_grenaded[bot_index])
					seen->grenade_hits++;
				basic_training.bot_grenaded[bot_index] = TRUE;
			}
		}
		basic_training.bot_unit[bot_index] = bot->unit_index;
		basic_training.bot_vitality[bot_index] = vitality;
	}
}

/* ---------- public code */

boolean basic_training_active(
	void)
{
	return basic_training.state != _training_off;
}

short basic_training_chapter(
	void)
{
	return basic_training.state == _training_playing ? basic_training.chapter : NONE;
}

static void basic_training_finished_read(
	void)
{
	if (!basic_training.finished_read)
	{
		basic_training.finished = basic_training_finished_parse(config_string("vr.training_finished"));
		basic_training.finished_read = TRUE;
	}
}

boolean basic_training_chapter_finished(
	short chapter)
{
	basic_training_finished_read();
	return chapter >= 0 && chapter < BASIC_TRAINING_CHAPTERS && (basic_training.finished & (1UL << chapter)) != 0;
}

char const *basic_training_chapter_name(
	short chapter)
{
	return chapter >= 0 && chapter < BASIC_TRAINING_CHAPTERS ? basic_training_chapters[chapter].name : "";
}

char const *basic_training_chapter_about(
	short chapter)
{
	return chapter >= 0 && chapter < BASIC_TRAINING_CHAPTERS ? basic_training_chapters[chapter].about : "";
}

boolean basic_training_choose(
	short chapter)
{
	if (!basic_training_chapter_ready(chapter))
		return FALSE;
	basic_training_finished_read();
	if (basic_training.state == _training_playing)
	{
		/* (jumped to at the next tick, the menus closed) */
		basic_training.pending_chapter = chapter;
		basic_training.pending_step = 0;
		ui_widgets_close_all();
		return TRUE;
	}
	if (basic_training.state == _training_starting)
		return TRUE;
	basic_training.state = _training_starting;
	basic_training.start_chapter = chapter;
	basic_training.start_step = 0;
	basic_training.start_seconds = 0.0f;
	basic_training.start_stage = 0;
	platform_log("training: starting at chapter %d", chapter);
	return TRUE;
}

boolean basic_training_player_held(
	long player_index)
{
	return basic_training.state != _training_off && bots_player_parked(player_index);
}

void basic_training_postspawn(
	long player_index)
{
	struct player_datum *player;

	if (basic_training.state != _training_playing || basic_training.chapter == NONE ||
		basic_training.pending_chapter != NONE || player_index != basic_training_trainee())
	{
		return;
	}
	player = player_get(player_index);
	if (player->unit_index != NONE && basic_training_field())
		basic_training_place_trainee(player->unit_index);
}

void basic_training_note_grip(
	real two_handed)
{
	basic_training.two_handed = two_handed;
}

/* the button that does a step's action on the player's layout: the steps
name the default layout's (A jumps, X reloads, the right trigger fires),
which the player's profile's preset moves, and out of the headset their own
layout too (input.controller_p1: controller_remap_prompt, as the HUD's
prompts); the controllers in the hands keep the profile's, which no layout
changes. The sticks are their moving and looking, kept */
static short basic_training_layout_button(
	short button)
{
	/* the game controls on the default layout's A, B, X, Y, black and
	white, and the triggers (input_abstraction.c's) */
	static short const controls[] = { 0, 4, 2, 3, 1, 5, 6, 7 };
	struct game_input_preferences preferences;
	short local_player_index = local_player_get_next(NONE);

	if (button < 0 || button >= (short)NUMBEROF(controls) || local_player_index == NONE)
		return button;
	input_abstraction_get_local_player_preferences(local_player_index, &preferences);
#ifdef HALO_VR
	if (!halo_vr_running())
#endif
		controller_remap_prompt(local_player_index, &preferences);
	return preferences.game_control_to_xbox_buttons[controls[button]];
}

/* the step's button as it is pressed: a grenade is thrown with the left
grip's swing in the headset (vr.grenade_throw "gesture" or "both"), and its
words then; else the button the player's layout throws one with */
static short basic_training_step_button(
	struct basic_training_step const *step)
{
#ifdef HALO_VR
	if (step->button == _training_button_left_trigger && halo_vr_running() &&
		strcmp(config_string("vr.grenade_throw"), "button") != 0)
	{
		return _training_button_left_grip;
	}
#endif
	return basic_training_layout_button(step->button);
}

static char const *basic_training_step_words(
	struct basic_training_step const *step)
{
	short button = basic_training_step_button(step);

	if (step->check == _training_check_grenade && button != _training_button_left_grip)
	{
		return button == _training_button_left_trigger || button == _training_button_right_trigger ?
			"Grenade: pull %s to throw one." : "Grenade: press %s to throw one.";
	}
	return step->words;
}

boolean basic_training_highlight(
	short *button,
	real *pulse)
{
	struct basic_training_step const *step;

	if (basic_training.state != _training_playing || basic_training.chapter == NONE ||
		basic_training.chapter_done || !game_in_progress())
	{
		return FALSE;
	}
	step = &basic_training_steps[basic_training.step];
	if (step->button == NONE)
		return FALSE;
	*button = basic_training_step_button(step);
	/* (twice a second, never quite out) */
	*pulse = 0.55f + 0.45f * (real)sin((real)(game_time_get() % TICKS_PER_SECOND) * 2.0f * TRAINING_PI /
		TICKS_PER_SECOND * 2.0f);
	return TRUE;
}

/* the start of a training game (from the main menu): as network_test.c's
"local:" game, a game of this machine alone, Team Slayer on Blood Gulch
without end, its bots basic training's */
static void basic_training_start_update(
	real seconds)
{
	basic_training.start_seconds += seconds;
	switch (basic_training.start_stage)
	{
	case 0:
		bots_set_training(TRUE);
		main_set_multiplayer_map_name(TRAINING_MAP_NAME);
		ui_widgets_close_all();
		dispose_global_network_game_server();
		dispose_global_network_game_client();
		network_game_accept_remote_connections(FALSE);
		game_engine_playlist_initialize();
		if (create_global_network_game_server())
		{
			game_engine_playlist_begin();
			game_connection_set(_game_connection_network_server);
		}
		if (!global_network_game_client_get())
			create_global_network_game_client();
		ui_widget_load_by_name_or_tag(
			pc_menus_screen("ui\\shell\\main_menu\\multiplayer_type_select\\split_screen\\pregame\\splitscreen_pregame_wrapper_normal"),
			NONE, NULL, NONE, NONE, NONE, NONE);
		basic_training.start_stage = 1;
		break;
	case 1:
		if (basic_training.start_seconds >= TRAINING_START_MAP_SECONDS && global_network_game_server_get())
		{
			struct game_variant variant;
			struct game_variant_options options;

			network_game_server_change_map_name(global_network_game_server_get(), TRAINING_MAP);
			variant = *game_engine_get_variant_by_name(&variant, "team_slayer");
			variant.universal_variant.score_to_win = 999;
			game_modes_options_default(&options);
			options.time_limit = 0;
			options.friendly_fire = _friendly_fire_off;
			player_ui_set_game_variant(&variant);
			player_ui_set_game_variant_options(&options);
			network_game_server_change_game_variant(global_network_game_server_get(), &variant);
			basic_training.start_stage = 2;
		}
		break;
	case 2:
		if (basic_training.start_seconds >= TRAINING_START_PLAYER_SECONDS && global_network_game_client_get())
		{
			network_game_client_add_player(global_network_game_client_get(), 0);
			basic_training.start_stage = 3;
		}
		break;
	case 3:
		if (basic_training.start_seconds >= TRAINING_START_GO_SECONDS)
		{
			network_game_client_request_immediate_start();
			basic_training.state = _training_playing;
			basic_training.chapter = NONE;
			basic_training.pending_chapter = basic_training.start_chapter;
			basic_training.pending_step = basic_training.start_step;
			basic_training.field_known = FALSE;
			basic_training.nav_looked = FALSE;
			basic_training.unit_index = NONE;
			basic_training.placed_weapons[0] = basic_training.placed_weapons[1] = NONE;
			platform_log("training: the game starts");
		}
		break;
	}
}

static void basic_training_stop(
	void)
{
	basic_training.state = _training_off;
	basic_training.chapter = NONE;
	basic_training.pending_chapter = NONE;
	bots_set_training(FALSE);
	platform_log("training: over");
}

/* debug.training: "<chapter>[:<step>]" started from the main menu */
static void basic_training_debug_start(
	boolean main_menu_loaded,
	real seconds)
{
	char const *setting;

	if (basic_training.debug_checked || !main_menu_loaded)
		return;
	setting = config_string("debug.training");
	if (!setting || !setting[0])
	{
		basic_training.debug_checked = TRUE;
		return;
	}
	if (bink_playback_in_progress())
	{
		bink_playback_stop();
		basic_training.menu_seconds = 0.0f;
		return;
	}
	basic_training.menu_seconds += seconds;
	if (basic_training.menu_seconds < 2.0f)
		return;
	basic_training.debug_checked = TRUE;
	{
		short chapter = (short)atoi(setting);
		char const *colon = strchr(setting, ':');

		if (basic_training_choose(chapter))
			basic_training.start_step = colon ? (short)atoi(colon + 1) : 0;
		else
			platform_log("training: debug.training \"%s\" names no chapter made", setting);
	}
}

/* the D-pad's left and right in a training game: the chapter before, or
the next (the HUD's prompt says so) */
static void basic_training_skip_buttons(
	void)
{
	struct gamepad_state const *pad = input_get_gamepad_state(0);
	boolean left = pad && pad->buttons[_gamepad_binary_button_dpad_left] > 0;
	boolean right = pad && pad->buttons[_gamepad_binary_button_dpad_right] > 0;

	if (basic_training.chapter != NONE && basic_training.pending_chapter == NONE && !ui_widgets_active() &&
		game_in_progress())
	{
		short next = NONE;

		if (right && !basic_training.dpad_right)
			next = basic_training_next_chapter(basic_training.chapter, 1);
		else if (left && !basic_training.dpad_left)
			next = basic_training_next_chapter(basic_training.chapter, -1);
		if (next != NONE)
		{
			basic_training.pending_chapter = next;
			basic_training.pending_step = 0;
		}
	}
	basic_training.dpad_left = left;
	basic_training.dpad_right = right;
}

void basic_training_update(
	boolean main_menu_loaded,
	real seconds)
{
	basic_training_debug_start(main_menu_loaded, seconds);
	switch (basic_training.state)
	{
	case _training_starting:
		if (main_menu_loaded)
			basic_training_start_update(seconds);
		break;
	case _training_playing:
		/* (left: back on the main menu, the game over) */
		if (main_menu_loaded && basic_training.chapter != NONE)
		{
			basic_training_stop();
			break;
		}
		basic_training_skip_buttons();
		/* (the fit check's screen, opened for its step: done once closed) */
		if (basic_training.chapter == 0 && game_in_progress() &&
			basic_training_steps[basic_training.step].check == _training_check_fit && !basic_training.chapter_done)
		{
#ifdef HALO_VR
			if (halo_vr_running() && !basic_training.fit_opened && !basic_training.fit_tried && !ui_widgets_active())
			{
				/* (once: a screen that cannot open passes the step) */
				basic_training.fit_tried = TRUE;
				basic_training.fit_opened = ui_widget_load_by_name_or_tag(TRAINING_FIT_SCREEN, NONE, NULL, 0, NONE, NONE,
					NONE) != NULL;
				if (!basic_training.fit_opened)
				{
					platform_log("training: the fit check's screen did not open");
					basic_training.seen.fitted = TRUE;
				}
			}
			else if (basic_training.fit_opened && !ui_widgets_active())
				basic_training.seen.fitted = TRUE;
#endif
		}
		break;
	}
}

void basic_training_tick(
	void)
{
	short result;
	struct basic_training_step const *step;

	if (basic_training.state != _training_playing || !game_engine_running() ||
		game_connection() != _game_connection_network_server)
	{
		return;
	}
	if (!basic_training_field())
		return;
	/* (a chapter asked for: set up once the trainee is in the game) */
	if (basic_training.pending_chapter != NONE)
	{
		long unit_index;

		if (!basic_training_trainee_unit(&unit_index))
			return;
		{
			short chapter = basic_training.pending_chapter;

			basic_training.pending_chapter = NONE;
			basic_training_chapter_begin(chapter, basic_training.pending_step);
		}
		return;
	}
	if (basic_training.chapter == NONE)
		return;
	basic_training_watch();
	if (basic_training.next_tick != NONE)
	{
		if (game_time_get() < basic_training.next_tick)
			return;
		if (basic_training.chapter_done)
		{
			short next = basic_training_next_chapter(basic_training.chapter, 1);

			basic_training.next_tick = NONE;
			if (next != NONE)
				basic_training_chapter_begin(next, 0);
		}
		else
			basic_training_step_begin();
		return;
	}
	if (basic_training.chapter_done)
		return;
	step = &basic_training_steps[basic_training.step];
	result = basic_training_step_result(step, &basic_training.seen);
	if (result == _training_result_done)
	{
		basic_training_message(step->done, FALSE);
		if (basic_training.step + 1 < TRAINING_STEP_COUNT &&
			basic_training_steps[basic_training.step + 1].chapter == basic_training.chapter)
		{
			game_engine_play_multiplayer_sound(_training_sound_countdown_timer);
			basic_training.step++;
			basic_training_step_begin();
		}
		else
			basic_training_finish_chapter();
	}
	else if (result == _training_result_failed)
	{
		basic_training_message(step->check == _training_check_targets ? "Out of rounds. Reset and go again." :
			"They got there first. Again!", TRUE);
		basic_training.next_tick = game_time_get() + (long)(TRAINING_RETRY_SECONDS * TICKS_PER_SECOND);
	}
}

void basic_training_render_nav_points(
	short local_player_index)
{
	long player_index;
	struct player_datum *player;
	real_point3d head;
	short spot;

	if (basic_training.state != _training_playing || basic_training.chapter == NONE ||
		basic_training_steps[basic_training.step].check != _training_check_spots || basic_training.chapter_done ||
		local_player_index != 0)
	{
		return;
	}
	if (!basic_training.nav_looked)
	{
		basic_training.nav_looked = TRUE;
		basic_training.nav_index = find_nav_point("target_blue");
	}
	player_index = local_player_get_player_index(local_player_index);
	player = player_index != NONE ? player_try_and_get(player_index) : NULL;
	if (basic_training.nav_index == NONE || !player || player->unit_index == NONE)
		return;
	unit_get_head_position(player->unit_index, &head);
	for (spot = 0; spot < TRAINING_SPOTS; spot++)
	{
		real_point3d point;

		if (basic_training.spot_reached[spot])
			continue;
		basic_training_field_point(training_spots[spot][0], training_spots[spot][1], &point);
		point.z += 0.7f;
		custom_render_nav_point(local_player_index, &point, basic_training.nav_index,
			hud_get_nav_point_render_type(local_player_index, &head, &point, NONE));
	}
}

/* ---------- the HUD */

static void basic_training_wide(
	char const *text,
	wchar_t *wide,
	short count)
{
	short index;

	for (index = 0; index < count - 1 && text[index]; index++)
		wide[index] = (wchar_t)(unsigned char)text[index];
	wide[index] = 0;
}

static void basic_training_draw_line(
	long font_index,
	rectangle2d const *bounds,
	short justification,
	char const *text,
	real_argb_color const *color)
{
	wchar_t wide[256];

	basic_training_wide(text, wide, (short)NUMBEROF(wide));
	draw_string_set_draw_mode(font_index, NONE, justification, 0, color);
	rasterizer_draw_unicode_string(bounds, NULL, NULL, 0, wide);
}

/* how wide the text is, in the font (draw_mode set) */
static short basic_training_text_width(
	char const *text,
	size_t count)
{
	char part[256];
	wchar_t wide[256];
	rectangle2d bounds, text_bounds, cursor;

	if (!count)
		return 0;
	count = MIN(count, sizeof(part) - 1);
	memcpy(part, text, count);
	part[count] = 0;
	basic_training_wide(part, wide, (short)NUMBEROF(wide));
	bounds.x0 = 0;
	bounds.y0 = 0;
	bounds.x1 = 0x3FFF;
	bounds.y1 = 0x3FFF;
	draw_unicode_string_compute_bounds(&bounds, wide, &text_bounds, &cursor);
	return (short)MAX(text_bounds.x1, cursor.x0);
}

/* the words broken into lines no wider than width, at most `most`; the
count of lines */
static short basic_training_wrap(
	long font_index,
	char const *text,
	short width,
	char lines[][160],
	short most)
{
	static real_argb_color const white = { 1.0f, 1.0f, 1.0f, 1.0f };
	short count = 0;

	draw_string_set_draw_mode(font_index, NONE, 0, 0, &white);
	while (*text && count < most)
	{
		size_t cut = 0, length = strlen(text);

		for (;;)
		{
			size_t next = cut;

			while (next < length && text[next] == ' ')
				next++;
			while (next < length && text[next] != ' ')
				next++;
			if (next == cut || next >= 159 || (cut && basic_training_text_width(text, next) > width))
				break;
			cut = next;
		}
		if (!cut)
			cut = MIN(length, (size_t)159);
		snprintf(lines[count], 160, "%.*s", (int)cut, text);
		count++;
		text += cut;
		while (*text == ' ')
			text++;
	}
	return count;
}

/* a button's name, as the HUD's prompts name it */
static void basic_training_button_name(
	short button,
	char *name,
	size_t size)
{
	static char const *const pad_names[NUMBER_OF_TRAINING_BUTTONS] =
	{
		"A", "B", "X", "Y", "Black", "White", "the left trigger", "the right trigger", "D-Pad Up", "D-Pad Down",
		"D-Pad Left", "D-Pad Right", "Start", "Back", "the left stick", "the right stick", "the left grip",
	};

	name[0] = 0;
	/* (an action the player's layout leaves without a button) */
	if (button == CONTROLLER_REMAP_UNBOUND)
	{
		snprintf(name, size, "%s", "its button (unbound: Settings > Controls > Controller)");
		return;
	}
#ifdef HALO_VR
	if (halo_vr_running() && halo_vr_button_name(button, name, (unsigned long)size))
		return;
#endif
	if (button >= 0 && button < NUMBER_OF_TRAINING_BUTTONS)
		snprintf(name, size, "%s", pad_names[button]);
	/* (an action on a back paddle: its name, "L4" on a Steam Deck) */
	else if (button >= CONTROLLER_REMAP_PROMPT_PADDLE && button <= CONTROLLER_REMAP_PROMPT_PADDLE + 3)
	{
		wchar_t const *text = controller_remap_prompt_text(local_player_get_next(NONE), button);
		size_t index;

		for (index = 0; text && text[index] && index + 1 < size; index++)
			name[index] = (char)text[index];
		name[index] = 0;
	}
}

/* the panel (TRAINING_PANEL_LINES of text) at the top of the middle of the
HUD, out of the reticle's way: the chapter and the step's place in it, the
step's words, its progress or the line of a step done; the skip buttons
under it. Laid out in the font's own size and drawn scaled, so that a line
is about a 26th of the view's height */
#define TRAINING_PANEL_LINES 7
#define TRAINING_WORD_LINES 4

void basic_training_draw(
	short local_player_index)
{
	struct basic_training_step const *step;
	long font_index = hud_get_font_index();
	struct font_header *font;
	rectangle2d window = render.camera.window_bounds;
	rectangle2d box, line;
	short line_height, width, height, left, top, inner, index, word_lines, margin;
	real scale;
	real_argb_color color, white, dim, good, bad;
	char text[256], button[64], words[256];
	char lines[TRAINING_WORD_LINES][160];
	short count, first;

	if (basic_training.state != _training_playing || basic_training.chapter == NONE || local_player_index != 0 ||
		font_index == NONE || ui_widgets_active() || cinematic_in_progress() ||
		(hud_scripted_globals && !hud_scripted_globals->show_hud))
	{
		return;
	}
	offset_rectangle2d(&window, (short)-render.camera.viewport_bounds.x0, (short)-render.camera.viewport_bounds.y0);
	width = (short)(window.x1 - window.x0);
	height = (short)(window.y1 - window.y0);
	font = font_definition_get(font_index);
	line_height = (short)(font->ascending_height + font->descending_height + font->leading_height);
	if (line_height <= 0 || width <= 0 || height <= 0)
		return;
	scale = PIN((real)height / 26.0f / (real)line_height, 0.3f, 1.5f);
	step = &basic_training_steps[basic_training.step];
	first = basic_training_first_step(basic_training.chapter, &count);
	hud_get_text_color(&color);
	color.alpha = 1.0f;
	white.alpha = 1.0f;
	white.red = white.green = white.blue = 0.95f;
	dim = color;
	dim.alpha = 0.8f;
	good.alpha = 1.0f;
	good.red = 0.45f;
	good.green = 1.0f;
	good.blue = 0.45f;
	bad.alpha = 1.0f;
	bad.red = 1.0f;
	bad.green = 0.45f;
	bad.blue = 0.35f;

	/* (the panel on the screen; within it, laid out unscaled from its top
	left, which the scale draws about) */
	/* (a wide screen's middle; the headset's HUD layer, about square, wider) */
	margin = (short)(width * (width * 2 > height * 3 ? 28 : 16) / 100);
	box.x0 = (short)(window.x0 + margin);
	box.x1 = (short)(window.x1 - margin);
	box.y0 = (short)(window.y0 + height * 12 / 100);
	box.y1 = (short)(box.y0 + (TRAINING_PANEL_LINES * line_height + line_height / 2) * scale);
	draw_quad(&box, 0x99000000);
	left = box.x0;
	top = box.y0;
	inner = (short)((box.x1 - box.x0) / scale - line_height);
	rasterizer_text_set_scale(scale, (real)left, (real)top);
	line.x0 = (short)(left + line_height / 2);
	line.x1 = (short)(line.x0 + inner);
	line.y0 = (short)(top + line_height / 4);
	line.y1 = (short)(line.y0 + line_height);
	snprintf(text, sizeof(text), "%d  %s", basic_training.chapter, basic_training_chapters[basic_training.chapter].name);
	basic_training_draw_line(font_index, &line, 0, text, &color);
	if (basic_training.chapter_done)
		snprintf(text, sizeof(text), "VR TUTORIAL  -  %s", basic_training.all_done ? "ALL DONE" : "CHAPTER COMPLETE");
	else if (step->challenge)
		snprintf(text, sizeof(text), "VR TUTORIAL  -  CHALLENGE");
	else
		snprintf(text, sizeof(text), "VR TUTORIAL  -  STEP %d OF %d", basic_training.step - first + 1, count);
	line.y0 = (short)(line.y1);
	line.y1 = (short)(line.y0 + line_height);
	basic_training_draw_line(font_index, &line, 0, text, basic_training.chapter_done ? &good : &dim);

	/* the words, wrapped */
	if (basic_training.chapter_done)
	{
		short next = basic_training_next_chapter(basic_training.chapter, 1);

		if (next == NONE)
			snprintf(words, sizeof(words), "%s Pause for the chapters, or leave the game.", step->done);
		else
			snprintf(words, sizeof(words), "%s Next: %s.", step->done, basic_training_chapters[next].name);
	}
	else
	{
		basic_training_button_name(basic_training_step_button(step), button, sizeof(button));
		basic_training_words(basic_training_step_words(step), button, words, sizeof(words));
	}
	word_lines = basic_training_wrap(font_index, words, inner, lines, TRAINING_WORD_LINES);
	for (index = 0; index < word_lines; index++)
	{
		line.y0 = (short)(line.y1);
		line.y1 = (short)(line.y0 + line_height);
		basic_training_draw_line(font_index, &line, 0, lines[index], &white);
	}

	/* the progress of a count, or the line of a step done or failed */
	line.y0 = (short)(top + line_height / 4 + (TRAINING_PANEL_LINES - 1) * line_height);
	line.y1 = (short)(line.y0 + line_height);
	text[0] = 0;
	if (basic_training.feedback[0] && game_time_get() - basic_training.feedback_tick <
		(long)(TRAINING_FEEDBACK_SECONDS * TICKS_PER_SECOND) && !basic_training.chapter_done)
	{
		basic_training_draw_line(font_index, &line, 0, basic_training.feedback,
			basic_training.feedback_failed ? &bad : &good);
	}
	else if (!basic_training.chapter_done)
	{
		struct basic_training_seen const *seen = &basic_training.seen;

		switch (step->check)
		{
		case _training_check_spots:
			snprintf(text, sizeof(text), "Spots %d of %d   %ld s", seen->spots, step->amount,
				(game_time_get() - basic_training.step_tick) / TICKS_PER_SECOND);
			break;
		case _training_check_targets:
			snprintf(text, sizeof(text), "Targets down %d of %d", seen->targets_down, step->amount);
			break;
		case _training_check_loadout:
			snprintf(text, sizeof(text), "Sniper rifle: %s   Pistol: %s",
				seen->sniper_hit ? "HIT" : seen->sniper_held ? "held" : "-",
				seen->pistol_hit ? "HIT" : seen->pistol_held ? "held" : "-");
			break;
		case _training_check_group:
			snprintf(text, sizeof(text), "Grenade hits %d of %d   Melee: %s", seen->grenade_hits, step->amount,
				seen->melee_hits ? "done" : "-");
			break;
		case _training_check_score:
			snprintf(text, sizeof(text), "You %d   Them %d   (first to %d)", seen->kills, seen->enemy_kills,
				step->amount);
			break;
		}
		if (text[0])
			basic_training_draw_line(font_index, &line, 0, text, &color);
	}

	/* the skip buttons, under the panel */
	{
		char next_name[64], back_name[64];

		basic_training_button_name(_training_button_dpad_right, next_name, sizeof(next_name));
		basic_training_button_name(_training_button_dpad_left, back_name, sizeof(back_name));
		snprintf(text, sizeof(text), "%s: next    %s: back    Pause: chapters", next_name, back_name);
		/* (wider than the panel: from a sixth of the way across) */
		line.x0 = (short)(left - (box.x0 - (window.x0 + width / 6)) / scale);
		line.x1 = (short)(left + (window.x1 - width / 6 - box.x0) / scale);
		line.y0 = (short)(top + TRAINING_PANEL_LINES * line_height + line_height * 3 / 4);
		line.y1 = (short)(line.y0 + line_height);
		basic_training_draw_line(font_index, &line, 2 /* centre */, text, &dim);
	}
	rasterizer_text_set_scale(1.0f, 0.0f, 0.0f);
}
