/*
SIM_RATE.H

The simulation's two clocks (game.sim_rate; sim_rate.c). The game's time
stays 30 ticks a second, as the maps, the scripts and the network were made
for: everything a tick does runs once a tick, as before. Inside each tick a
fast phase runs 1, 2 or 4 sub-steps (game.sim_rate 30, 60 or 120), where
what a player feels in their hands can later move in smaller steps: their
own movement and aim, projectiles, thrown items, the VR mode's throws and
grabs. The clock spreads a tick's sub-steps over the frames as their time
passes (sim_substep_next, below).

Rules for what moves into the fast phase:

- Per-tick quantities stay per tick in the game state (velocities in world
  units a tick, timers in ticks): the ticks, the network, saved games and
  the scripts read them. A sub-step takes its share with the conversions
  below, each of which is exactly its argument at 30 (and an exact scaling
  by a power of two at 60 and 120, for the linear and quadratic ones).
- Nothing of a sub-step outlives its tick except what a tick at 30 would
  have left in the game state: a game saved (at a tick's end) is the same
  layout at every rate, and a tick at 30 runs exactly as it did.
- A counter of ticks (airborne_ticks, elevator_ticks, a held button's
  ticks: a byte or a short in the game state, which four times the counts
  would overflow) goes on counting ticks: code moved into the fast phase
  counts it on the last sub-step only (sim_substep_is_last).
- Timing that is authored in ticks (rates of fire, animations' frames, the
  AI's and the scripts' timers) stays on the tick.
- No test of the rate itself outside sim_rate.c: the count, the fractions
  and the conversions say all a sub-step needs.
*/

#ifndef SIM_RATE_H
#define SIM_RATE_H

/* ---------- constants */

enum
{
	/* the rate the game was made for, and its rate in any game that must stay
	as it was (sim_rate_get) */
	SIM_RATE_CLASSIC = 30,
	MAXIMUM_SIM_SUBSTEPS = 4
};

/* the fast phase's work, in the order each sub-step runs it: one procedure a
slot, registered by the subsystem that owns it (sim_substep_register). A
new slot goes where its work must run relative to the others' */
enum sim_substep_slot
{
	_sim_substep_objects, /* the lanes': lifts' riders carried (objects.c) */
	_sim_substep_player_control, /* the players' input and aim */
	_sim_substep_vehicles, /* the players' vehicles, and what they carry */
	_sim_substep_units, /* the players' units: aim, throws */
	_sim_substep_bipeds, /* the players' bipeds' movement */
	_sim_substep_weapons, /* the players' shots, at the sub-step pulled */
	_sim_substep_items, /* thrown items and grenades */
	_sim_substep_projectiles,
	_sim_substep_vr, /* the VR mode's throws and grabs */
	_sim_substep_network, /* an OpenH1 game's sends of each sub-step (network_substeps.c) */
	_sim_substep_render_interpolation, /* a pose kept after each sub-step */
	_sim_substep_trace, /* the scorecard's trace (debug.trace) */
	_sim_substep_network_test, /* the network test's presses (debug.network_test) */
	NUMBER_OF_SIM_SUBSTEP_SLOTS
};

/* ---------- types */

/* a slot's work for one sub-step: which (0 up to count - 1), of how many,
each seconds long */
typedef void (*sim_substep_procedure)(short substep_index, short substep_count, real seconds);

/* ---------- prototypes */

/* whether a rate is one the sub-steps step at: 30 times a power of two up
to MAXIMUM_SIM_SUBSTEPS (30, 60 or 120). A network game's agreed rate and
the settings are taken only so */
boolean sim_rate_supported(long rate);

/* the rate this tick simulates at, 30, 60 or 120: game.sim_rate, but in a
network game of other machines the rate it agreed on (a Legacy game's 30,
whatever the host or client; a Standard game's host's choice:
sim_rate_network) and 30 in a saved film's playback */
long sim_rate_get(void);

/* whether the game played (or about to be, from a lobby) is of this machine
alone and takes game.sim_rate: a game of its own connection, or split
screen (a network game of one machine that takes no other) */
boolean sim_rate_follows_setting(void);

/* whether game.sim_rate is set aside: the game played (or about to be, from
a lobby) takes its rate from its connection, not the setting: a network
game of other machines or a film's playback. The menus' SIMULATION row
shows it locked at the rate the game takes meanwhile (sim_rate_next: a
Legacy game's ORIGINAL (30), a Standard game's agreed rate;
menu_functions.c's sim_rate_row_show) */
boolean sim_rate_locked(void);

/* the sub-steps of this tick (1, 2 or 4), and the one running: 0 up to
count - 1 in the fast phase, NONE outside it (between two sub-steps of a
tick too, in the frames: sim_substep_next) */
short sim_substep_count(void);
short sim_substep_index(void);
/* whether this is the tick's last sub-step, or no sub-step (what counts
ticks counts them here) */
boolean sim_substep_is_last(void);
/* a sub-step's length: in seconds (1 / rate), and as a share of a tick
(1 / count) */
real sim_substep_seconds(void);
real sim_substep_tick_fraction(void);
/* how far into the tick the running sub-step ends, 0 to 1 ((index + 1) /
count; 1 outside the fast phase): where an object that stays on the tick is
seen from it, between its poses of the tick before and this one */
real sim_substep_end_fraction(void);
/* whether the tick's own work before its fast phase (game.c's macro step,
which is its first sub-step's: it ends as that sub-step does) is running */
boolean sim_substep_in_macro_step(void);

/* a quantity per 30 Hz tick as one per sub-step, each exactly its argument
at 30:
linear: a distance, a turn or a change of a velocity in a tick, a rate per
tick (velocity stays per tick: position += velocity * tick_fraction)
quadratic: an acceleration per tick per tick, as one per sub-step per
sub-step, for a velocity a sub-step keeps per sub-step (gravity)
exponential: a factor applied each tick (a decay, friction, a spring's
damping) as the factor that, applied each sub-step, makes the same in a
tick; between 0 and 1 (another is returned as it is: keep it on the tick)
chance: a chance per tick as the chance per sub-step that gives the same
chance of happening in a tick */
real sim_per_substep(real per_tick);
real sim_per_substep_squared(real per_tick_squared);
real sim_decay_per_substep(real factor_per_tick);
real sim_chance_per_substep(real chance_per_tick);
/* a number of ticks as sub-steps */
long sim_ticks_to_substeps(long ticks);

/* the fast phase's work for a slot (one procedure a slot, NULL takes it
away): called each sub-step of each tick, at every rate, 30's one too */
void sim_substep_register(short slot, sim_substep_procedure procedure);

/* the frames between the ticks (render_interpolation.c): a frame drawn a
tick_fraction of the way from the tick before's end to this tick's, as the
sub-step whose poses it is between (0 up to count - 1: the pose before it
is the end of sub-step - 1, the tick before's end for 0) and how far from
it, 0 to 1. At 30 that is sub-step 0 and the tick_fraction itself */
void sim_substep_blend(real tick_fraction, short *substep_index, real *substep_fraction);

/* The sub-steps spread over the frames (game_time.c): the clock runs a
sub-step each time a sub-step's time has passed, not a whole tick at once.
The tick's own work (game_tick's but for the fast phase) runs with its first
sub-step; the rest run on the frames that follow as their time comes, the
tick's work after the fast phase with its last, so a tick runs in the order
it always has. At 120 on a display of 120 Hz that is one sub-step a frame,
each with the input that frame read. Between two sub-steps of a tick the
tick is open: what the frames do to the game as a whole (save it, load
another, change the map) waits for its end (main.c).

the open tick's next sub-step, 0 when none is open (the next to run begins
a tick) */
short sim_substep_next(void);
/* whether the running sub-step is the first since the frame's input was
read (player_control_update, before game_time_update runs the frame's
sub-steps): what a sub-step after it in the same frame reads of the
controller is the frame's, or the scripted player's at its own time */
boolean sim_substep_first_of_frame(void);
/* the rate of the next sub-step: the open tick's, else the one the next tick
would take now (a frame of debug.fixed_step lasts one sub-step of it) */
long sim_rate_next(void);

/* the clock's (game_time.c): a frame's input read, and an open tick given
up (a game state loaded over it, a map ended) */
void sim_rate_frame_begin(void);
void sim_rate_tick_drop(void);
/* the tick loop's (game.c): the rate taken for the tick, at its start, and
one sub-step of the fast phase (each of a tick's once, in order) */
void sim_rate_tick_begin(void);
void sim_substep_run(short substep_index);

#endif
