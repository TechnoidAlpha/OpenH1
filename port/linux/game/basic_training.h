/* basic_training.h: the VR build's VR Tutorial (port/linux/game/basic_training.c)

The main menu's VR TUTORIAL (the VR build's, in the headset) opens its
chapters; a chapter chosen starts a game of this machine alone on Blood
Gulch with four bots, which the training drives. */
#ifndef HALO_BASIC_TRAINING_H
#define HALO_BASIC_TRAINING_H

/* the chapters, 0 to 7 (5 and 6 not yet made: basic_training_chapter_ready) */
#define BASIC_TRAINING_CHAPTERS 8
/* the left grip, a step's "button" after the controller's (which
halo_vr_button_name numbers 0 to 15): VR's own input, no button of the game */
#define BASIC_TRAINING_LEFT_GRIP 16

/* the menus (menu_functions.c): a chapter chosen, in a training game (it is
jumped to: set up, the chapters before it not played) or from the main menu
(a training game started at it); FALSE for a chapter not made yet */
boolean basic_training_choose(short chapter);
/* a training game is played (or starting) */
boolean basic_training_active(void);
/* the chapter played, NONE out of training */
short basic_training_chapter(void);
boolean basic_training_chapter_ready(short chapter);
boolean basic_training_chapter_finished(short chapter);
char const *basic_training_chapter_name(short chapter);
char const *basic_training_chapter_about(short chapter);

/* main.c, each frame: the start of a training game, the menus' screens it
opens, the skip buttons (debug.training starts one) */
void basic_training_update(boolean main_menu_loaded, real seconds);
/* game.c, each tick, after the game engine's: the steps' checks, the
chapters' set up */
void basic_training_tick(void);
/* game_engine.c: a player kept from spawning (a parked bot), and a player
spawned (the trainee put at the chapter's place, with its weapons) */
boolean basic_training_player_held(long player_index);
void basic_training_postspawn(long player_index);
/* game_engine.c's nav points: the spots to run to */
void basic_training_render_nav_points(short local_player_index);
/* hud.c: the step's words, the progress and the skip buttons */
void basic_training_draw(short local_player_index);
/* port/linux/game/vr_hands.c (HALO_VR): the controller button the step
names (halo_vr_button_name's numbering), to be lit on the controller in the
hand, and how bright (0 to 1, pulsing); and the long gun's foregrip held */
boolean basic_training_highlight(short *button, real *pulse);
void basic_training_note_grip(real two_handed);

#endif
