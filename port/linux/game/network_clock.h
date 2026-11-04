/*
NETWORK_CLOCK.H

An OpenH1 game's client clock, led ahead of the host's by its trip there
(network_clock.c; NETCODE.md, "The client's clock").
*/

#ifndef __NETWORK_CLOCK_H
#define __NETWORK_CLOCK_H
#pragma once

/* ---------- constants */

enum
{
	/* a tick, in microseconds */
	CLOCK_TICK_MICROSECONDS = 1000000 / TICKS_PER_SECOND,
	/* how long before the host's tick of its number a client's input is to
	come (the figure of each report below), in microseconds: a frame's
	reading of the datagrams on the host is in the figure already. Under
	the least the clock moves ahead to the target, over the most back toward
	it, and between the two it stays as it is */
	CLOCK_EARLY_TARGET_MICROSECONDS = 2000,
	CLOCK_EARLY_LOW_MICROSECONDS = 1000,
	CLOCK_EARLY_HIGH_MICROSECONDS = 5000,
	/* the host reports to each OpenH1 client once a window of this many of
	its ticks, of no fewer inputs than these, the figure of the window's
	inputs this far up from the earliest (of 100: the second of a second's
	30, so that one input held up does not move the clock) */
	CLOCK_REPORT_TICKS = TICKS_PER_SECOND,
	CLOCK_REPORT_MINIMUM_SAMPLES = 8,
	CLOCK_REPORT_PERCENTILE = 5,
	/* (the most a window keeps, and the inputs heard ahead of the host's
	ticks, and the host's ticks begun, it keeps the times of: powers of two) */
	CLOCK_WINDOW_SAMPLES = 64,
	CLOCK_PENDING_TICKS = 16,
	CLOCK_BEGUN_TICKS = 64,
	/* a client: past this far behind (microseconds), its clock skips the
	whole ticks of the difference and runs the rest of it at once; nearer,
	and ahead, it runs faster or slower, by no more than this part of the
	time, until it has moved by this part of the difference ... */
	CLOCK_JUMP_MICROSECONDS = 50000,
	/* ... and its ticks no further ahead of the host's latest it has had
	than this (the host's clock check, CLIENT_CLOCK_AHEAD_TICKS, is 15) */
	CLOCK_LEAD_LIMIT_TICKS = 12,
	/* a report's figure, no further either way (microseconds) */
	CLOCK_SAMPLE_LIMIT_MICROSECONDS = 1000000,
	/* the message's format: a machine takes its own alone */
	CLOCK_FORMAT = 1,
};
#define CLOCK_SLEW 0.05f
#define CLOCK_GAIN 0.5f

/* ---------- structures */

/* on the wire, the host's to an OpenH1 client, reliably, once a window: how
long before the host's tick of the same number its inputs came, the
window's figure (negative: after it began), and of how many */
struct network_clock_message
{
	byte format;
	byte pad;
	short samples;
	long early_microseconds;
};

/* what a client does on a report: its ticks skipped at the end of the
tick (renumbered, none run), and the time it moves its clock by, at once
(ahead) or over the frames */
struct network_clock_plan
{
	long ticks;
	real seconds;
	boolean at_once;
};

/* ---------- prototypes/NETWORK_CLOCK.C */

void network_clock_new_game(void);
/* (the host) a client machine new at its index */
void network_clock_machine_reset(long machine_index);
/* (the host) the input of a client machine's tick heard now, its first */
void network_clock_input_heard(long machine_index, long tick);
/* a tick begun (network_distributed_tick_begun) */
void network_clock_tick_begun(void);
/* (the host, each frame once its datagrams are read:
network_distributed_frame_received) */
void network_clock_frame_read(void);
/* (the host, in its tick) the reports due sent */
void network_clock_host_tick(void);
/* (a client, at its tick's end) the ticks to skip skipped */
void network_clock_tick_ended(void);
/* (a client, game_time_update) the time this frame's ticks take more (or
less) than the frame's to move the clock as planned */
real network_clock_frame_seconds(real time_delta_sec);
/* (a dedicated server, main.c) whether its frame waits for its time before
its datagrams are read: an OpenH1 game's; and, once a frame, whether it did */
boolean network_clock_server_pace_first(void);
boolean network_clock_server_paced(void);
boolean network_clock_handles_message(word const *message, word size);
void network_clock_handle_message(long machine_index, word const *message, word size, boolean from_stream);
/* (a client: the network test's log) the latest report's figure
(microseconds, NONE for none), the reports taken, the ticks skipped and
the time moved by in all (seconds) */
void network_clock_statistics(long *early_microseconds, long *reports, long *ticks_skipped, real *seconds_moved);

/* the pure parts: a window's figure of its samples (sorted here); a
client's plan for a report, its ticks ahead of the host's latest it has
had; the part of the time planned a frame takes */
long network_clock_window_figure(long *samples, short count);
void network_clock_plan_report(long early_microseconds, long ticks_ahead, struct network_clock_plan *plan);
real network_clock_frame_part(real *seconds, boolean *at_once, real time_delta_sec);

#endif
