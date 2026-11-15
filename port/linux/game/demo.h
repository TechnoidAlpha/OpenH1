/*
DEMO.H

Demos (demo.c, docs/demos.md): every game of this machine recorded as it
plays, its ticks' input and what reached its game state from outside them,
and played again exactly.
*/

#ifndef __HALO_PORT_DEMO_H
#define __HALO_PORT_DEMO_H

struct player_action;

/* each game state allocation, as game_state.c makes it */
void demo_game_state_allocation(char const *name, void const *address, long size);

/* the game state rewritten as a whole outside the ticks: a saved game or a
core loaded, a checkpoint reverted to (game_state.c's after-load
procedures), the map started over (main.c), a new map (game.c) */
void demo_note_state_loaded(void);
void demo_note_new_map(void);

/* the clock's ticks (game_time.c): before a tick's first sub-step, and
once its last has run */
void demo_tick_begin(long tick);
void demo_tick_end(long tick);

/* a tick left open between two of its sub-steps, the frames meanwhile
(game_time.c: game.sim_rate's sub-steps spread over the frames), and the
next of them begun: what the frames put into the game state in between
(a host's netcode dealing a client's reported hits) */
void demo_tick_pause(void);
void demo_substep_resume(short substep_index);

/* the tick's players' input (players.c): TRUE if a demo's playback gave it
(the bots then take no part); else, around the bots' turn, it is recorded */
boolean demo_playback_tick_actions(struct player_action *actions);
void demo_record_tick_actions_begin(void);
void demo_record_tick_actions(struct player_action const *actions);

/* a sub-step past the tick's first (player_control.c's slot): the players'
units' controls as it began and as it ended */
void demo_substep_begin(short substep_index);
void demo_substep_end(short substep_index, short substep_count);
/* in playback, a unit's controls that went down in the running sub-step */
unsigned long demo_playback_pressed_flags(long unit_index);

/* inside a tick, what the netcode puts into the game state from what other
machines sent (a host takes its clients' predictions of their units:
network_distributed.c): recorded as it changed the game state, and in
playback written as it did (the netcode then has nothing to put in) */
void demo_foreign_begin(void);
void demo_foreign_end(void);
/* inside a tick, a choice the netcode makes by what other machines are
there (a host leaves a client's hits to its reports: network_damage.c):
recorded, and in playback the recording's */
boolean demo_decide(boolean decision);
/* ... and a choice the frames' state makes for the tick (whether a
particle waits for a later frame, whose emitter's snapshots the frames
keep: render_interpolation.c), in any game: recorded, and in playback the
recording's */
boolean demo_decide_frames(boolean decision);
/* ... and numbers the tick takes from the frames' state (a unit's
illumination, the lights'): recorded, and in playback the recording's */
void demo_decide_reals(long object_index, real *values, short count);

/* outside the ticks, an event of the netcode's that its code applies to
the game state (a client's hit the host deals: network_damage.c), begun
and ended around that code: recorded as the event at its place among the
frames' differences, and in playback done again there by the same code
(network_damage_deal_recorded). TRUE if it was recorded */
boolean demo_event_begin(char type, void const *bytes, unsigned long size);
void demo_event_end(void);

/* (the host) after a tick, the netcode's work that puts what other machines
sent into the game state (network_distributed.c: the predictions of the
clients whose units it moves after the tick, an OpenCE machine's, or every
client's at 30 ticks a second), begun and ended around it: recorded as the
bytes it changed, an M record before the next tick, which a playback
writes in there, not as a whole game state for each unit it moved */
void demo_netcode_begin(void);
void demo_netcode_end(void);

/* whether a demo is playing (the input of the machine's players, its own
saved games, its recording left alone), and the rate its ticks run at (0
when none is: sim_rate.c) */
boolean demo_playing(void);
long demo_playback_sim_rate(void);
/* the rules of the recorded game that this machine's settings would
otherwise give a game of its own (network_standard.c): TRUE in a demo's
playback, with the recording's in rules (its header's) */
enum
{
	_demo_rule_enhanced_animations_bit,
	_demo_rule_turns_with_aim_bit,
	_demo_rule_walk_over_pickup_bit,
	_demo_rule_simulation_fixes_bit,
	_demo_rule_vehicle_hijack_bit,
};
boolean demo_playback_rules(unsigned long *rules);
/* ... and in a host's recording's playback the network game its host had
at each tick (its record, which a rule changed during the game is in:
network_standard_vehicle_hijack); NULL otherwise */
struct network_game *demo_playback_recorded_game(void);
/* the connection the ticks of the demo playing run as (main.c's
game_connection): a co-op game this machine hosted plays its ticks as its
host's, whose code decides the game's co-op parts (network_coop.c, the
co-op files, players.c); FALSE otherwise (the machine's own: a local game) */
boolean demo_playback_connection(short *connection);
/* ... and the network game its ticks read then, the host's as the recording
had it (network_game_globals.c's network_game_get_game: its players, its
co-op switches); NULL otherwise */
struct network_game *demo_playback_network_game(void);

/* each frame (main.c): a demo asked for (demo.play, --demo) started, and
the playback's end */
void demo_frame(boolean main_menu_loaded, real seconds);

/* the clip key: the demo being recorded or played marked here (its last
demo.clip_seconds) */
void demo_bookmark(void);
/* a line of the host's to its bots, applied in the tick (bots_command.c):
recorded where it was, for demo_info.py and the playback's log */
void demo_record_bot_command(char const *line);

#endif
