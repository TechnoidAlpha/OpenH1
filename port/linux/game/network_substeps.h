/*
NETWORK_SUBSTEPS.H

An OpenH1 game's sub-step inputs (network_substeps.c).
*/

#ifndef NETWORK_SUBSTEPS_H
#define NETWORK_SUBSTEPS_H

/* ---------- prototypes */

/* whether this tick is a network game's of several sub-steps (an OpenH1
game at 60 or 120), whose players' sub-steps go between the machines */
boolean network_substeps_active(void);

/* a new game: nothing heard or kept of the last */
void network_substeps_new_game(void);

/* the fast phase (player_control.c's slot): its first sub-step, before
which each player's tick input was taken, and each later one, after the
local players' units took their sub-step's reading */
void network_substeps_tick_begun(short substep_count);
void network_substeps_replay(short substep_index, short substep_count);
/* a local player's unit given its sub-step's reading (player_control.c):
its facing as given, and whether the unit's aim turned to it */
void network_substeps_local_given(long player_index, long unit_index, short substep_index, short substep_count,
	real_euler_angles2d const *facing, boolean turned);
/* the controls of a remote player's unit that went down in the running
sub-step, as its record has them (player_control_substep_pressed_flags'
for the units no local player controls) */
unsigned long network_substeps_pressed_flags(long unit_index);

/* after each tick (network_distributed.c): a client's own players' sub-steps
to the host; the host's of the players it relays to a client this tick (by
absolute index), with the update their input is of */
void network_substeps_client_send(void);
void network_substeps_host_send(long machine_index, long update_number, short const *player_indices, short count);
/* a client's player's input forwarded as it comes (network_distributed.c):
(the host) the sub-steps it heard of the player's tick with it, their count
then the relayed record's bytes (a count of 0 alone for none); the bytes
written, at most 1 + RELAYED_SUBSTEP_SIZE * (MAXIMUM_SIM_SUBSTEPS - 1) (31).
(a client) the same read and kept, no further than end, of that tick: the
bytes read, 0 for too few */
enum
{
	NETWORK_SUBSTEPS_FORWARD_MAXIMUM_SIZE = 31,
};
word network_substeps_forward_write(short player_index, long key, byte *buffer);
word network_substeps_forward_read(short player_index, long key, byte const *data, byte const *end);

/* An OpenH1 game's controls as each sub-step runs (NETCODE.md, "Sub-steps as
they run"): each machine's own players' at each sub-step's end where they
changed (a client's to the host at once, with the hits the sub-step made),
which the host sends on to its clients as they come, with its own players'
(network_distributed.c's forwarded input), and a client gives another
player's unit while they are later than the input its tick took. Not at 30.

(the host) a player's latest not yet sent on, in LIVE_CONTROL_FORWARD_SIZE
bytes (0: none), and all of them sent; whether any is to send */
enum
{
	LIVE_CONTROL_FORWARD_SIZE = 19,
};
word network_substeps_live_forward_write(short player_index, byte *buffer);
/* (the host) a player's controls as its own sub-steps ran them, at their
latest press (the held controls changed, or a stick or the trigger moved
far), for a client its plan does not send the player's every control to
(NETCODE.md, "Presses to far clients"): last, what was last sent that
client; whether this is to go (then last is it), and its bytes */
struct live_control_far
{
	boolean valid;
	word held_flags;
	short throttle_i;
	short throttle_j;
	short primary_trigger;
};
boolean network_substeps_live_far_due(short player_index, struct live_control_far *last);
word network_substeps_live_far_write(short player_index, byte *buffer);
/* (the host) whether it has had live controls of the player's own (a
client's player's, or its own player's; not a bot's) */
boolean network_substeps_live_own(short player_index);
void network_substeps_live_forwarded(void);
boolean network_substeps_live_pending(void);
/* (a client, after each tick's actions are given to the units: game.c) the
other players' latest controls given their units where they are later */
void network_substeps_actions_given(void);

/* (a client) the tick of another client's player whose forwarded input its
last tick ran (the input's client's), NONE for none: the relayed input, or
none, drove it */
long network_substeps_forwarded_key(short player_index);

/* the messages (network_distributed.c's handling, before its own) */
boolean network_substeps_handles_message(word const *message, word size);
void network_substeps_handle_message(long machine_index, word const *message, word size);

/* the bytes of sub-step messages this machine sent since it was last asked
(the tests' bandwidth figures) */
long network_substeps_bytes_sent(void);

#endif
