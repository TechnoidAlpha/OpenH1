/*
HOST_STALL.C

The stall detector (debug.stall_ms, HALO_STALL_MS; 100 by default, 0 off):
a watchdog thread that notices when the game has gone that long without
handing the render thread a frame, and finds out why. While the frame is
late it samples, every 10 ms (every 40 ms after the first second), the
game's thread (the one whose frames these are: on macOS the process's main
thread, which pumps Cocoa's events too) and the render thread: their
program counters and frame chains, taken by the platform's sampler
(host_debug.c's signal, host_debug_win.c's suspension). Once the frame
comes, one block of the log says how long the game waited, how many frames
have been that late, and where the two threads were, the samples grouped by
their frame chains, the commonest first, each address named: a guest one
as the guest's (llvm-symbolizer --obj=<build>/halo_guest.elf), a host one
as its library's symbol, or the executable's offset (atos -o <halo> -l 0
or llvm-symbolizer). The work that was in progress (host_stall_note:
pipelines and shaders made and waited for, the loading screen) is listed
with it. A stall the loading screen was up for is a load: one line, its
time. The report (F8) has the host's log, and so these blocks.

Between stalls it costs the game a clock reading and a store each frame,
and the watchdog a wake-up every 10 ms.
*/

#include "host.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STALL_SAMPLES 512
#define STALL_RETURNS 12
#define STALL_NOTES 32
#define STALL_GROUPS_SHOWN 4
#define STALL_CHAIN_SHOWN 10

struct stall_sample
{
	uint64_t pc, lr;
	uint64_t returns[STALL_RETURNS];
	uint8_t thread, count;
	/* (written last: the record is whole) */
	volatile uint8_t done;
};

struct stall_note
{
	uint64_t at;
	const char *what;
};

static struct
{
	uint64_t threshold;
	/* the latest frame's time (0: none yet), the frames over the threshold,
	the longest gap */
	uint64_t frame_at;
	uint64_t over, longest;
	/* the loading screen up (host_stall_loading); the stalls that were
	loads, and the longest of the others */
	int loading;
	uint64_t loads, longest_stall;
	/* (the watchdog's) a stall being sampled, from when; its end, as the
	frame that ended it came (set by host_stall_frame) */
	int sampling, loading_from;
	uint64_t stalled_from, ended_gap;
	/* the samples of the stall (the platform's sampler fills them through
	host_stall_record) */
	struct stall_sample samples[STALL_SAMPLES];
	int sample_next;
	/* the work in progress, the latest STALL_NOTES (host_stall_note) */
	struct stall_note notes[STALL_NOTES];
	unsigned note_next;
	pthread_mutex_t notes_lock;
} stall = { .notes_lock = PTHREAD_MUTEX_INITIALIZER };

static const char *const thread_names[HOST_STALL_THREADS] = { "the game's thread", "the render thread" };

static uint64_t now_nanoseconds(void)
{
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);
	return (uint64_t)time.tv_sec * 1000000000ull + (uint64_t)time.tv_nsec;
}

void host_stall_frame(void)
{
	uint64_t now, previous;

	if (!stall.threshold)
		return;
	now = now_nanoseconds();
	previous = __atomic_exchange_n(&stall.frame_at, now, __ATOMIC_ACQ_REL);
	if (!previous)
	{
		host_debug_stall_thread(HOST_STALL_GAME);
		return;
	}
	if (now - previous >= stall.threshold)
	{
		__atomic_add_fetch(&stall.over, 1, __ATOMIC_RELAXED);
		if (now - previous > stall.longest)
			stall.longest = now - previous;
		__atomic_store_n(&stall.ended_gap, now - previous, __ATOMIC_RELEASE);
	}
}

void host_stall_render_thread(void)
{
	if (stall.threshold)
		host_debug_stall_thread(HOST_STALL_RENDER);
}

void host_stall_loading(int active)
{
	__atomic_store_n(&stall.loading, active, __ATOMIC_RELEASE);
	host_stall_note(active ? "the loading screen shown" : "the loading screen ended");
}

void host_stall_note(const char *what)
{
	unsigned index;

	if (!stall.threshold)
		return;
	pthread_mutex_lock(&stall.notes_lock);
	index = stall.note_next++ % STALL_NOTES;
	stall.notes[index].at = now_nanoseconds();
	stall.notes[index].what = what;
	pthread_mutex_unlock(&stall.notes_lock);
}

/* (the platform's sampler; a signal handler on Linux, Android and macOS:
async-signal-safe) */
void host_stall_record(int thread, uint64_t pc, uint64_t lr, const uint64_t *returns, int count)
{
	int index;
	struct stall_sample *sample;

	if (!__atomic_load_n(&stall.sampling, __ATOMIC_ACQUIRE))
		return;
	index = __atomic_fetch_add(&stall.sample_next, 1, __ATOMIC_ACQ_REL);
	if (index >= STALL_SAMPLES)
		return;
	sample = &stall.samples[index];
	sample->pc = pc;
	sample->lr = lr;
	if (count > STALL_RETURNS)
		count = STALL_RETURNS;
	if (count < 0)
		count = 0;
	memcpy(sample->returns, returns, (size_t)count * sizeof(*returns));
	sample->count = (uint8_t)count;
	sample->thread = (uint8_t)thread;
	__atomic_store_n(&sample->done, 1, __ATOMIC_RELEASE);
}

/* (the samples grouped) whether two samples have the same frame chain:
their return addresses (a wait or a loop has one; its program counter
moves about in it) */
static int same_chain(const struct stall_sample *a, const struct stall_sample *b)
{
	int index;

	if (a->thread != b->thread || a->count != b->count)
		return 0;
	if (!a->count)
		return a->lr == b->lr;
	for (index = 0; index < a->count; index++)
	{
		if (a->returns[index] != b->returns[index])
			return 0;
	}
	return 1;
}

static void describe(uint64_t address, char *buffer, size_t size)
{
	if (host_in_guest((uintptr_t)address) && (uint32_t)address >= host_image.base &&
		(uint32_t)address < host_image.end)
		snprintf(buffer, size, "guest %x", (unsigned)(uint32_t)address);
	else
		host_debug_describe_address(address, buffer, size);
}

static void report(uint64_t gap)
{
	int count = __atomic_load_n(&stall.sample_next, __ATOMIC_ACQUIRE), index, other, thread, shown;
	int group_of[STALL_SAMPLES], group_size[STALL_SAMPLES], per_thread[HOST_STALL_THREADS] = { 0 };
	unsigned note, notes_shown = 0;
	char line[2048], name[256];
	int length, load = stall.loading_from;

	if (count > STALL_SAMPLES)
		count = STALL_SAMPLES;
	/* (a load: the loading screen up at any time of it) */
	pthread_mutex_lock(&stall.notes_lock);
	for (note = 0; note < STALL_NOTES; note++)
	{
		const struct stall_note *entry = &stall.notes[note];

		if (entry->what && entry->at >= stall.stalled_from && !strcmp(entry->what, "the loading screen shown"))
			load = 1;
	}
	pthread_mutex_unlock(&stall.notes_lock);
	if (load)
	{
		/* (the game loads behind the loading screen: its time logged, not
		where it was) */
		stall.loads++;
		host_logf(HOST_LOG_INFO, "stall: a load: the game made no frame for %.0f ms, the loading screen up",
			(double)gap / 1e6);
		return;
	}
	if (gap > stall.longest_stall)
		stall.longest_stall = gap;
	host_logf(HOST_LOG_WARN, "stall: the game made no frame for %.0f ms (%llu frames of %llu ms or more so far, "
		"%llu of them loads; the longest other %.0f ms); %d samples", (double)gap / 1e6,
		(unsigned long long)stall.over, (unsigned long long)(stall.threshold / 1000000ull),
		(unsigned long long)stall.loads, (double)stall.longest_stall / 1e6, count);
	/* (the work in progress during it: each note of the stall's time) */
	pthread_mutex_lock(&stall.notes_lock);
	length = snprintf(line, sizeof(line), "stall: work during it:");
	for (note = 0; note < STALL_NOTES; note++)
	{
		const struct stall_note *entry = &stall.notes[(stall.note_next + note) % STALL_NOTES];

		if (!entry->what || entry->at < stall.stalled_from)
			continue;
		if (length > 0 && (size_t)length < sizeof(line))
			length += snprintf(line + length, sizeof(line) - length, " %s (at %.0f ms);", entry->what,
				(double)(entry->at - stall.stalled_from) / 1e6);
		notes_shown++;
	}
	pthread_mutex_unlock(&stall.notes_lock);
	if (notes_shown)
		host_log(HOST_LOG_WARN, line);
	for (index = 0; index < count; index++)
	{
		group_of[index] = index;
		group_size[index] = 0;
		if (!stall.samples[index].done)
			continue;
		for (other = 0; other < index; other++)
		{
			if (stall.samples[other].done && group_of[other] == other &&
				same_chain(&stall.samples[other], &stall.samples[index]))
			{
				group_of[index] = other;
				break;
			}
		}
		group_size[group_of[index]]++;
		if (stall.samples[index].thread < HOST_STALL_THREADS)
			per_thread[stall.samples[index].thread]++;
	}
	for (thread = 0; thread < HOST_STALL_THREADS; thread++)
	{
		for (shown = 0; shown < STALL_GROUPS_SHOWN; shown++)
		{
			int best = -1, chain;
			const struct stall_sample *sample;

			for (index = 0; index < count; index++)
			{
				if (group_size[index] > 0 && stall.samples[index].thread == thread &&
					(best < 0 || group_size[index] > group_size[best]))
					best = index;
			}
			if (best < 0)
				break;
			sample = &stall.samples[best];
			describe(sample->pc, name, sizeof(name));
			length = snprintf(line, sizeof(line), "stall: %s, %d of %d samples: %s", thread_names[thread],
				group_size[best], per_thread[thread], name);
			if (sample->lr && sample->count == 0)
			{
				describe(sample->lr, name, sizeof(name));
				length += snprintf(line + length, sizeof(line) - length, " < %s", name);
			}
			for (chain = 0; chain < sample->count && chain < STALL_CHAIN_SHOWN && length > 0 &&
				(size_t)length < sizeof(line); chain++)
			{
				describe(sample->returns[chain], name, sizeof(name));
				length += snprintf(line + length, sizeof(line) - length, " < %s", name);
			}
			host_log(HOST_LOG_WARN, line);
			group_size[best] = 0;
		}
	}
}

static void *watchdog(void *argument)
{
	(void)argument;
	for (;;)
	{
		struct timespec interval = { 0, 10000000 };
		uint64_t at, now, gap;

		while (nanosleep(&interval, &interval) != 0 && errno == EINTR)
			;
		at = __atomic_load_n(&stall.frame_at, __ATOMIC_ACQUIRE);
		if (!at)
			continue;
		now = now_nanoseconds();
		if (!stall.sampling)
		{
			if (now - at < stall.threshold)
				continue;
			stall.stalled_from = at;
			stall.loading_from = __atomic_load_n(&stall.loading, __ATOMIC_ACQUIRE);
			memset(stall.samples, 0, sizeof(stall.samples));
			__atomic_store_n(&stall.sample_next, 0, __ATOMIC_RELEASE);
			__atomic_store_n(&stall.ended_gap, 0, __ATOMIC_RELEASE);
			__atomic_store_n(&stall.sampling, 1, __ATOMIC_RELEASE);
		}
		if (at != stall.stalled_from)
		{
			/* (a frame came: the samples' handlers given a moment to finish) */
			struct timespec settle = { 0, 2000000 };

			gap = __atomic_load_n(&stall.ended_gap, __ATOMIC_ACQUIRE);
			nanosleep(&settle, NULL);
			__atomic_store_n(&stall.sampling, 0, __ATOMIC_RELEASE);
			report(gap ? gap : at - stall.stalled_from);
			continue;
		}
		/* (every 10 ms for the first second, then every 40) */
		if (now - at < stall.threshold + 1000000000ull || (now / 10000000ull) % 4 == 0)
		{
			host_debug_stall_interrupt(HOST_STALL_GAME);
			host_debug_stall_interrupt(HOST_STALL_RENDER);
		}
	}
	return NULL;
}

void host_stall_start(double milliseconds)
{
	pthread_t thread;

	if (!(milliseconds > 0.0) || stall.threshold)
		return;
	if (milliseconds < 20.0)
		milliseconds = 20.0;
	stall.threshold = (uint64_t)(milliseconds * 1e6);
	host_debug_stall_prepare();
	if (pthread_create(&thread, NULL, watchdog, NULL) != 0)
	{
		stall.threshold = 0;
		return;
	}
	pthread_detach(thread);
}

void host_stall_summary(void)
{
	if (stall.threshold && stall.frame_at)
		host_logf(HOST_LOG_INFO, "stall: %llu frames of %llu ms or more, %llu of them loads; the longest %.0f ms, "
			"the longest other %.0f ms", (unsigned long long)stall.over,
			(unsigned long long)(stall.threshold / 1000000ull), (unsigned long long)stall.loads,
			(double)stall.longest / 1e6, (double)stall.longest_stall / 1e6);
}
