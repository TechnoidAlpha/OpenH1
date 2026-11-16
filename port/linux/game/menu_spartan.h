/*
MENU_SPARTAN.H

The menus' Spartan (menu_spartan.c): drawn by the engine, from ui.map's
Spartan tags (menu_models.h), in a view of its own among the menus' widgets.
*/

#ifndef MENU_SPARTAN_H
#define MENU_SPARTAN_H

enum
{
	/* at once, on every screen open: the split screen's four players side
	by side, a lobby's lineup of eight and a player's card */
	MAXIMUM_MENU_SPARTANS = 12,
	/* (side by side in one <spartan>, at most) */
	MAXIMUM_MENU_SPARTAN_PLACES = 8,
	/* a Spartan's color for a random profile color: the profile colors in
	turn, in their list's order, about one a second (menu_spartan_color) */
	MENU_SPARTAN_RANDOM = -1,
	/* a team game's colors, as the game gives its players theirs
	(game_engine_player_get_change_color: red's team red, blue's blue) */
	MENU_SPARTAN_TEAM_RED = -4,
	MENU_SPARTAN_TEAM_BLUE = -5,
};

/* whether ui.map has the Spartan's tags (else the screens draw the
original pictures) */
boolean menu_spartan_available(void);
/* a Spartan asked for this frame (the screen that shows it asks each frame
it draws): placed in the menus' 640 by 480, in its screen's units (its
screen's place on the screen is menu_spartan_draw's), its feet's middle at
x, bottom, as tall as height, in the profile color color_index (or
MENU_SPARTAN_RANDOM, or a team's: MENU_SPARTAN_TEAM_RED, _BLUE), one of a
lineup or not (a lineup's each turned, nearer or further a little, from
its place; one alone hardly turned). Each is drawn in a view of its own,
about its box, its idle and now and then a fidget on the menus' clock */
void menu_spartan_place(short index, short color_index, real x, real bottom, real height, boolean lineup);
/* how much lower the Spartan index stands than its bottom, in parts of its
height (a lineup's place a little nearer, lower; further, higher): where its
feet are, for what is drawn under them (a lineup's names) */
real menu_spartan_feet_lower(short index, boolean lineup);
/* the armor's color of the Spartan index in the profile color color_index
at the menus' frame time milliseconds: the color's own, or for
MENU_SPARTAN_RANDOM the colors in turn, each held most of a second then
blended into the next, each Spartan a part of the round on from the one
before it */
real_rgb_color menu_spartan_color(short index, short color_index, unsigned long milliseconds);
/* at the start of each frame the menus draw: the Spartans asked for made
and placed on their stage, the others deleted */
void menu_spartan_frame(void);
/* the Spartans first to first + count - 1 drawn now, each in its view,
among the menus' widgets (above those drawn before, under those after):
their screen at x, y of the menus' 640 by 480 */
void menu_spartan_draw(short x, short y, short first, short count);
/* one of the Spartans' objects, or what one carries (the scene's view
leaves them out: render_objects.c) */
boolean menu_spartan_object(long object_index);
/* the nodes an object is drawn with: a Spartan's (or its rifle's) posed
on the menus' clock while its view draws it, else nodes (render_objects.c) */
real_matrix4x3 const *menu_spartan_node_matrices(long object_index, real_matrix4x3 const *nodes);
/* debug: HALO_MENU_SPARTAN_TEST's colors ("<color> ...", up to eight), in
place of a <spartan>'s players' (0: not set) */
short menu_spartan_test_colors(short colors[MAXIMUM_MENU_SPARTAN_PLACES]);

#endif
