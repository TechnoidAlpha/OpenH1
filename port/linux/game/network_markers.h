/*
NETWORK_MARKERS.H

Pings (network_markers.c): a player marks the spot or the thing they aim
at for their team, with Halo's nav point and its distance.
*/

#ifndef NETWORK_MARKERS_H
#define NETWORK_MARKERS_H

/* each frame (main.c): the players' presses, the host's settings to its
clients, the pings faded out */
void network_markers_update(void);
/* a view's pings (hud_nav_points.c's hud_render_nav_points) */
void network_markers_render(short local_player_index);
/* whether the controller of the port pings with its stock layout's D-pad
Right now (xinput_sdl.c then keeps that direction from the game) */
unsigned char network_markers_pad_open(short port);
/* the message (network_distributed.c): OpenH1's own, in an OpenH1 game */
boolean network_markers_handles_message(word const *message, word size);
void network_markers_handle_message(long machine_index, word const *message, word size, boolean from_stream);

/* (the tests: debug.network_test_ping) the first local player pings what
it aims at, as a press does; FALSE if it could not */
boolean network_markers_ping_aimed(void);
/* ... (a client) one sent as another machine's player's, as a changed build
could send it, for the host to refuse; and count sent at once, past the
client's own limit, for the host to count */
boolean network_markers_ping_forged(void);
short network_markers_ping_burst(short count);
/* ... and the pings this machine shows now: each one's player (absolute
index), and the count */
short network_markers_shown(short *player_indices, short maximum);

/* the pure parts (tools/harness): a point the host takes (inside the world,
not too far from the player), and whether a ping of a player clears their
last (the same object, or a point near the last) */
boolean network_markers_point_valid(real x, real y, real z);
boolean network_markers_same_spot(long object_index, real const *point, long last_object_index,
	real const *last_point);

#endif
