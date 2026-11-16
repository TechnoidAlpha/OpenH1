/*
SPARTAN_LINK.H

The HUD's Spartan link: the living teammates' views in the HUD's top line
(spartan_link.c).
*/

#ifndef __SPARTAN_LINK_H
#define __SPARTAN_LINK_H

struct render_camera;

/* once a frame (main.c): the key's presses, each window's teammates, and
which feed is drawn this frame */
void spartan_link_update(void);

/* whether a port's controller presses the link (the stock layouts' D-pad
Down, which the game then does not get): its player is in a game a link
can be in (xinput_sdl.c) */
unsigned char spartan_link_pad_open(short port);

/* render.c: the feed drawn this frame, if it is the window's: its camera
(and its window, the feed's size) filled, and its images made the screen's
targets (halo_view_begin) until spartan_link_feed_end */
boolean spartan_link_feed_begin(short local_player_index, struct render_camera *camera);
void spartan_link_feed_end(void);

/* while a feed is drawn: the teammate whose body is hidden from it
(render_objects.c), and whose weapon and arms are posed in it
(first_person_weapons.c), else NONE */
long spartan_link_hidden_unit(void);
/* a feed is being drawn: what the views' drawing changes of the game state
(an antenna's or a flag's steps, antenna.c, flags.c) it leaves alone */
boolean spartan_link_drawing(void);
long spartan_link_posed_unit(void);
/* the units the shown feeds show (each once, the downed left out), whose
weapon and arms first_person_weapons.c animates in its feeds' slots, a tick
at a time: their count, at most maximum_count */
short spartan_link_linked_units(long *unit_indices, short maximum_count);

/* the window's link drawn in its HUD (render.c, after the rest of the HUD) */
void spartan_link_draw(short local_player_index);

#endif
