/*
NETWORK_CLOCK.C

An OpenH1 game's client clock, led ahead of the host's (OpenH1 issue #287,
stage 2e; NETCODE.md, "The client's clock").

A client's clock starts with the host's first game update and takes the
host's time from it, so its ticks begin about a trip from the host after
the host's of the same number, at a phase against the host's ticks that the
network's delay sets for the whole game. Its input of a tick then waited on
the host for the host's next tick to begin: from nothing to a whole tick,
by the game. Here the host times, for each OpenH1 client, how long before
the host's tick of the same number each of its inputs came (a client sends
its input of a tick as the tick's first sub-step ends, or at 30 as it
ends: about when the tick begins), and tells the client the figure of each
second's (_distributed_message_clock_phase, reliably). The client moves its
clock until its inputs come CLOCK_EARLY_TARGET_MICROSECONDS before the
host's tick of their number: its clock leads the host's by the trip there
and that margin, as other engines' do, and the host's tick takes the input
of the client's tick of its own number, with each sub-step's controls just
before the host's sub-step runs. The move is by running the ticks a little
faster or slower (CLOCK_SLEW at most); behind by more than
CLOCK_JUMP_MICROSECONDS (a game's start, the round trip grown), the clock
skips the whole ticks of the difference (renumbered, none run) and runs the
rest at once.

Only an OpenH1 game's host and clients (network_standard_agreed_rate) do
this, at every rate, and the host reports only to OpenH1 machines: a Legacy
game, and an OpenCE machine in a mixed game, play as before.
*/

#include "cseries.h"
#include "cseries/errors.h"
#include "game/game.h"
#include "networking/network_game_globals.h"
#include "network_distributed.h"
#include "network_standard.h"
#include "network_clock.h"
#include "sim_rate.h"

/* xbox_kernel.c's and game_time.c's */
unsigned long long platform_shared_clock_microseconds(void);
void game_time_port_advance(long ticks);

/* ---------- globals */

/* the host: each client machine's latest tick of input heard; the inputs
heard before the host's tick of their number began, when, and in which of
the host's frames (NONE: none); and the window's samples */
static struct network_clock_machine
{
	long last_tick;
	struct
	{
		long tick;
		unsigned long long heard;
		unsigned long frame;
	} pending[CLOCK_PENDING_TICKS];
	short count;
	long samples[CLOCK_WINDOW_SAMPLES];
} clock_machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];

/* the host: when its last ticks began, and its latest begun (NONE: none) */
static struct
{
	long tick;
	unsigned long long began;
} clock_begun[CLOCK_BEGUN_TICKS];
static long clock_latest_tick = NONE;
static unsigned long long clock_latest_began;
/* the host: its frames, counted as each frame's datagrams have been read
(network_clock_frame_read) */
static unsigned long clock_frame;

/* a client: the host's tick of the latest report taken (NONE: none), the
move planned and not yet made, and what the network test logs */
static struct
{
	long report_time;
	struct network_clock_plan plan;
	long early_microseconds;
	long reports;
	long ticks_skipped;
	real seconds_moved;
} clock_client;

/* ---------- the pure parts */

long network_clock_window_figure(
	long *samples,
	short count)
{
	short index;

	if (count <= 0)
		return 0;
	/* (insertion: a window's few) */
	for (index = 1; index < count; index++)
	{
		long sample = samples[index];
		short other = index;

		while (other > 0 && samples[other - 1] > sample)
		{
			samples[other] = samples[other - 1];
			other--;
		}
		samples[other] = sample;
	}
	return samples[count * CLOCK_REPORT_PERCENTILE / 100];
}

void network_clock_plan_report(
	long early_microseconds,
	long ticks_ahead,
	struct network_clock_plan *plan)
{
	long early = PIN(early_microseconds, -CLOCK_SAMPLE_LIMIT_MICROSECONDS, CLOCK_SAMPLE_LIMIT_MICROSECONDS);
	/* (how much further ahead the clock is to be: behind, positive) */
	long error = CLOCK_EARLY_TARGET_MICROSECONDS - early;

	plan->ticks = 0;
	plan->at_once = FALSE;
	if (error > CLOCK_JUMP_MICROSECONDS)
	{
		/* (the nearest whole ticks skipped, as far as the lead allows; the
		rest, under half a tick either way, run at once when it is ahead,
		else over the frames) */
		long ticks = (error + CLOCK_TICK_MICROSECONDS / 2) / CLOCK_TICK_MICROSECONDS;
		long rest = error - ticks * CLOCK_TICK_MICROSECONDS;

		plan->ticks = PIN(ticks, 0, MAX(0, CLOCK_LEAD_LIMIT_TICKS - ticks_ahead));
		plan->at_once = rest > 0;
		plan->seconds = (real)rest / 1000000.0f * (plan->at_once ? 1.0f : CLOCK_GAIN);
	}
	else if (early < CLOCK_EARLY_LOW_MICROSECONDS)
	{
		/* (late, or nearly: all of it, an input that misses the host's
		tick costs the player a tick) */
		plan->seconds = (real)error / 1000000.0f;
	}
	else if (early > CLOCK_EARLY_HIGH_MICROSECONDS)
	{
		/* (early: part of it, the next report's window began before the
		move) */
		plan->seconds = (real)error / 1000000.0f * CLOCK_GAIN;
	}
	else
	{
		/* (between: as it is. A host reads its datagrams once a frame, so
		every input that came in a frame is timed from that frame's read: a
		clock held to one figure would go back and forth across a frame) */
		plan->seconds = 0.0f;
	}
}

real network_clock_frame_part(
	real *seconds,
	boolean *at_once,
	real time_delta_sec)
{
	real part;
	real limit = CLOCK_SLEW * (time_delta_sec > 0.0f ? time_delta_sec : 0.0f);

	if (*at_once)
	{
		part = *seconds;
		*seconds = 0.0f;
		*at_once = FALSE;
		return part;
	}
	part = PIN(*seconds, -limit, limit);
	*seconds -= part;
	return part;
}

/* ---------- public code */

void network_clock_new_game(
	void)
{
	long machine_index;
	short index;

	for (machine_index = 0; machine_index < HALO_PORT_MAXIMUM_NETWORK_MACHINES; machine_index++)
		network_clock_machine_reset(machine_index);
	for (index = 0; index < CLOCK_BEGUN_TICKS; index++)
		clock_begun[index].tick = NONE;
	clock_latest_tick = NONE;
	clock_latest_began = 0;
	csmemset(&clock_client, 0, sizeof(clock_client));
	clock_client.report_time = NONE;
	clock_client.early_microseconds = NONE;
}

void network_clock_machine_reset(
	long machine_index)
{
	struct network_clock_machine *machine;
	short index;

	if (machine_index < 0 || machine_index >= HALO_PORT_MAXIMUM_NETWORK_MACHINES)
		return;
	machine = &clock_machines[machine_index];
	machine->last_tick = NONE;
	for (index = 0; index < CLOCK_PENDING_TICKS; index++)
		machine->pending[index].tick = NONE;
	machine->count = 0;
}

/* (the host) whether it times the machine's inputs: an OpenH1 game's, an
OpenH1 machine's */
static boolean network_clock_hosted_machine(
	long machine_index)
{
	return game_connection() == _game_connection_network_server && network_standard_agreed_rate() &&
		machine_index >= 0 && machine_index < HALO_PORT_MAXIMUM_NETWORK_MACHINES &&
		network_standard_machine_is_openh1(machine_index);
}

static void network_clock_sample(
	struct network_clock_machine *machine,
	long long early)
{
	if (machine->count >= CLOCK_WINDOW_SAMPLES)
		return;
	machine->samples[machine->count++] = (long)PIN(early,
		-(long long)CLOCK_SAMPLE_LIMIT_MICROSECONDS, (long long)CLOCK_SAMPLE_LIMIT_MICROSECONDS);
}

void network_clock_input_heard(
	long machine_index,
	long tick)
{
	struct network_clock_machine *machine;
	unsigned long long now;

	if (!network_clock_hosted_machine(machine_index) || clock_latest_tick == NONE)
		return;
	machine = &clock_machines[machine_index];
	/* (each tick's first input; one far behind the last, its game begun
	again) */
	if (machine->last_tick != NONE && tick <= machine->last_tick &&
		tick >= machine->last_tick - 10 * TICKS_PER_SECOND)
	{
		return;
	}
	machine->last_tick = tick;
	now = platform_shared_clock_microseconds();
	if (tick > clock_latest_tick && tick - clock_latest_tick < CLOCK_PENDING_TICKS)
	{
		/* (the host's tick of its number not begun: timed when it does) */
		machine->pending[tick & (CLOCK_PENDING_TICKS - 1)].tick = tick;
		machine->pending[tick & (CLOCK_PENDING_TICKS - 1)].heard = now;
		machine->pending[tick & (CLOCK_PENDING_TICKS - 1)].frame = clock_frame;
	}
	else
	{
		/* (begun: when, if it is one of the last, else as far from the
		latest's beginning as their ticks are apart) */
		long long began = (long long)clock_latest_began +
			(long long)(tick - clock_latest_tick) * CLOCK_TICK_MICROSECONDS;

		if (tick <= clock_latest_tick && clock_begun[tick & (CLOCK_BEGUN_TICKS - 1)].tick == tick)
			began = (long long)clock_begun[tick & (CLOCK_BEGUN_TICKS - 1)].began;
		network_clock_sample(machine, began - (long long)now);
	}
}

void network_clock_tick_begun(
	void)
{
	long tick;
	unsigned long long now;
	long machine_index;

	if (game_connection() != _game_connection_network_server || !network_standard_agreed_rate())
		return;
	tick = game_time_get();
	now = platform_shared_clock_microseconds();
	clock_begun[tick & (CLOCK_BEGUN_TICKS - 1)].tick = tick;
	clock_begun[tick & (CLOCK_BEGUN_TICKS - 1)].began = now;
	clock_latest_tick = tick;
	clock_latest_began = now;
	for (machine_index = 0; machine_index < HALO_PORT_MAXIMUM_NETWORK_MACHINES; machine_index++)
	{
		struct network_clock_machine *machine = &clock_machines[machine_index];
		long pending = machine->pending[tick & (CLOCK_PENDING_TICKS - 1)].tick;

		if (pending == NONE)
			continue;
		/* (one of a tick the host's clock skipped dropped) */
		/* (read in the frame the tick begins in, just before it: as early as
		it need be, however long before the read it came, which the host
		cannot tell: the target. Read in a frame before, how long before) */
		if (pending == tick && machine->pending[tick & (CLOCK_PENDING_TICKS - 1)].frame + 1 == clock_frame)
			network_clock_sample(machine, CLOCK_EARLY_TARGET_MICROSECONDS);
		else if (pending == tick)
			network_clock_sample(machine, (long long)now - (long long)machine->pending[tick & (CLOCK_PENDING_TICKS - 1)].heard);
		machine->pending[tick & (CLOCK_PENDING_TICKS - 1)].tick = NONE;
	}
}

void network_clock_frame_read(
	void)
{
	clock_frame++;
}

void network_clock_host_tick(
	void)
{
	long machine_index;

	if (game_connection() != _game_connection_network_server || !network_standard_agreed_rate() ||
		game_time_get() % CLOCK_REPORT_TICKS != 0)
	{
		return;
	}
	for (machine_index = 0; machine_index < HALO_PORT_MAXIMUM_NETWORK_MACHINES; machine_index++)
	{
		struct network_clock_machine *machine = &clock_machines[machine_index];

		if (machine->count >= CLOCK_REPORT_MINIMUM_SAMPLES && network_clock_hosted_machine(machine_index))
		{
			struct
			{
				struct distributed_message_header header;
				struct network_clock_message report;
			} message;

			csmemset(&message, 0, sizeof(message));
			message.report.format = CLOCK_FORMAT;
			message.report.samples = machine->count;
			message.report.early_microseconds = network_clock_window_figure(machine->samples, machine->count);
			distributed_send_to_machine_reliably(machine_index, &message, _distributed_message_clock_phase, 1,
				(word)sizeof(message));
		}
		machine->count = 0;
	}
}

void network_clock_tick_ended(
	void)
{
	if (game_connection() != _game_connection_network_client || clock_client.plan.ticks <= 0 ||
		sim_substep_next())
	{
		return;
	}
	game_time_port_advance(clock_client.plan.ticks);
	clock_client.ticks_skipped += clock_client.plan.ticks;
	clock_client.plan.ticks = 0;
}

real network_clock_frame_seconds(
	real time_delta_sec)
{
	real part;

	if (game_connection() != _game_connection_network_client || !network_standard_agreed_rate() || game_time_held())
		return 0.0f;
	part = network_clock_frame_part(&clock_client.plan.seconds, &clock_client.plan.at_once, time_delta_sec);
	clock_client.seconds_moved += part;
	return part;
}

/* a dedicated server of an OpenH1 game: whether this frame waits for its
time before its datagrams are read (main.c), not after; and, once, whether
it did */
static boolean clock_server_paced;

boolean network_clock_server_pace_first(
	void)
{
	clock_server_paced = game_connection() == _game_connection_network_server && network_standard_agreed_rate();
	return clock_server_paced;
}

boolean network_clock_server_paced(
	void)
{
	boolean paced = clock_server_paced;

	clock_server_paced = FALSE;
	return paced;
}

boolean network_clock_handles_message(
	word const *message,
	word size)
{
	struct distributed_message_header header;

	if (size < sizeof(header))
		return FALSE;
	csmemcpy(&header, message, sizeof(header));
	return header.type == _distributed_message_clock_phase;
}

/* (the host's alone, over its stream, whole, in an OpenH1 game under way) */
void network_clock_handle_message(
	long machine_index,
	word const *message,
	word size,
	boolean from_stream)
{
	struct distributed_message_header header;
	struct network_clock_message report;
	long host_time = distributed_latest_host_time();

	if (machine_index != NONE || !from_stream || game_connection() != _game_connection_network_client ||
		!network_standard_agreed_rate() || !game_in_progress() || game_time_held() ||
		size != sizeof(header) + sizeof(report))
	{
		return;
	}
	csmemcpy(&header, message, sizeof(header));
	csmemcpy(&report, (byte const *)message + sizeof(header), sizeof(report));
	if (header.count != 1 || report.format != CLOCK_FORMAT)
		return;
	/* (none older than one taken) */
	if (clock_client.report_time != NONE && header.game_time <= clock_client.report_time)
		return;
	clock_client.report_time = header.game_time;
	clock_client.early_microseconds = PIN(report.early_microseconds,
		-CLOCK_SAMPLE_LIMIT_MICROSECONDS, CLOCK_SAMPLE_LIMIT_MICROSECONDS);
	clock_client.reports++;
	network_clock_plan_report(clock_client.early_microseconds,
		host_time == NONE ? 0 : game_time_get() - host_time, &clock_client.plan);
	if (clock_client.plan.ticks || clock_client.plan.at_once)
	{
		error(_error_silent, "clock: the host heard this machine's input %ld us before its tick: %ld ticks skipped, %ld us run at once",
			clock_client.early_microseconds, clock_client.plan.ticks,
			clock_client.plan.at_once ? (long)(clock_client.plan.seconds * 1000000.0f) : 0L);
	}
}

void network_clock_statistics(
	long *early_microseconds,
	long *reports,
	long *ticks_skipped,
	real *seconds_moved)
{
	*early_microseconds = clock_client.early_microseconds;
	*reports = clock_client.reports;
	*ticks_skipped = clock_client.ticks_skipped;
	*seconds_moved = clock_client.seconds_moved;
}
