/*
ASSERT_REPORT.C

Every failed assertion and warning of a run, counted by its place
(cseries.c's release_assert_failed and display_assert call here). A release
build carries on past a failed assertion, as the retail game did, so code
after one runs with the value it rejected; a debug build stops on it (it
halts). Players get exactly that. For tests, debug.fatal_asserts
(HALO_FATAL_ASSERTS) makes a failed assertion end the run instead, in
either build: its reason, place and the call stack's return addresses on
stderr (and debug.txt), then exit status ASSERT_REPORT_EXIT_CODE. A warning
is counted only.

Whatever the setting, a run in which any failed is summed up on stderr when
it exits: each place, how many times, assert or warn. The determinism log
writes the totals (determinism_log.c) and the profiling build's reports
carry them (profile_console.c).
*/

#include "cseries.h"
#include "errors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* port_config.c */
int config_boolean(const char *name);
void platform_log(const char *format, ...);

enum
{
	MAXIMUM_ASSERT_REPORT_PLACES = 512,
	MAXIMUM_ASSERT_REPORT_INFORMATION = 120,
	MAXIMUM_ASSERT_REPORT_FRAMES = 24,
	/* (EX_SOFTWARE: apart from a crash's 128 + signal and the dedicated
	server's 0 to 5) */
	ASSERT_REPORT_EXIT_CODE = 70
};

struct assert_report_place
{
	char const *file;
	long line;
	unsigned long count;
	boolean fatal;
	char information[MAXIMUM_ASSERT_REPORT_INFORMATION];
};

static struct
{
	volatile long lock;
	int fatal_checked;
	int fatal_on;
	int summary_registered;
	unsigned long failed;
	unsigned long warned;
	unsigned long unplaced;
	struct assert_report_place places[MAXIMUM_ASSERT_REPORT_PLACES];
} assert_report;

static __thread int assert_report_reporting;

static void assert_report_summary(void);

/* the totals so far: failed assertions and failed warnings */
void assert_report_counts(
	unsigned long *failed,
	unsigned long *warned)
{
	*failed = __atomic_load_n(&assert_report.failed, __ATOMIC_RELAXED);
	*warned = __atomic_load_n(&assert_report.warned, __ATOMIC_RELAXED);
}

/* a failure counted: how many times its place has failed, this one included
(0 for a place the full table has no room for) */
unsigned long assert_report_failed(
	char const *information,
	char const *file,
	long line,
	boolean fatal)
{
	unsigned long index = ((unsigned long)(size_t)file + (unsigned long)line * 2654435761UL) % MAXIMUM_ASSERT_REPORT_PLACES;
	unsigned long count = 0;
	unsigned long probe;
	int first = 0;

	while (__sync_lock_test_and_set(&assert_report.lock, 1))
		;
	if (fatal)
		assert_report.failed++;
	else
		assert_report.warned++;
	for (probe = 0; probe < MAXIMUM_ASSERT_REPORT_PLACES; probe++)
	{
		struct assert_report_place *place = &assert_report.places[index];

		if (!place->file)
		{
			place->file = file;
			place->line = line;
			place->fatal = fatal;
			/* (copied: a debug build's message may be a buffer formatted
			for this failure alone, csprintf's temporary) */
			snprintf(place->information, sizeof(place->information), "%s", information ? information : "<no reason given>");
		}
		if (place->file == file && place->line == line)
		{
			count = ++place->count;
			break;
		}
		index = (index + 1) % MAXIMUM_ASSERT_REPORT_PLACES;
	}
	if (!count)
		assert_report.unplaced++;
	if (!assert_report.summary_registered)
	{
		assert_report.summary_registered = 1;
		first = 1;
	}
	__sync_lock_release(&assert_report.lock);
	if (first)
		atexit(assert_report_summary);
	return count;
}

/* the call stack's return addresses, from the caller's frame up (the
guests are built with frame pointers: each frame's record is the caller's
frame pointer, then the return address, two 64-bit words); not under a base
register, whose frame pointer is the host's */
static long assert_report_frames(
	unsigned long *addresses,
	long maximum)
{
	long count = 0;
#if defined(HALO_GUEST) && !defined(HALO_GUEST_BASE) && (defined(__x86_64__) || defined(__aarch64__))
	unsigned long long frame = (unsigned long long)(size_t)__builtin_frame_address(0);

	while (count < maximum && frame && !(frame & 7) && frame <= 0xffffffffULL)
	{
		unsigned long long const *record = (unsigned long long const *)(size_t)frame;
		unsigned long long next = record[0];
		unsigned long long address = record[1];

		if (!address || address > 0xffffffffULL)
			break;
		addresses[count++] = (unsigned long)address;
		if (next <= frame || next - frame > 0x100000ULL)
			break;
		frame = next;
	}
#else
	addresses[count++] = (unsigned long)(size_t)__builtin_return_address(0);
#endif
	return count;
}

static int assert_report_fatal_on(
	void)
{
	if (!assert_report.fatal_checked)
	{
		assert_report.fatal_on = config_boolean("debug.fatal_asserts") != 0;
		assert_report.fatal_checked = 1;
	}
	return assert_report.fatal_on;
}

/* after the failure is logged: under debug.fatal_asserts, a failed
assertion ends the run (a warning carries on) */
void assert_report_logged(
	char const *information,
	char const *file,
	long line,
	boolean fatal)
{
	unsigned long addresses[MAXIMUM_ASSERT_REPORT_FRAMES];
	char text[24 * MAXIMUM_ASSERT_REPORT_FRAMES + 1];
	long frames;
	long index;
	long length = 0;

	if (!fatal || assert_report_reporting || !assert_report_fatal_on())
		return;
	assert_report_reporting = 1;
	frames = assert_report_frames(addresses, MAXIMUM_ASSERT_REPORT_FRAMES);
	text[0] = 0;
	for (index = 0; index < frames && length < (long)sizeof(text) - 12; index++)
		length += snprintf(text + length, sizeof(text) - length, " 0x%lx", addresses[index]);
	platform_log("fatal assert (debug.fatal_asserts): %s at %s:%ld", information ? information : "<no reason given>",
		file, line);
	platform_log("fatal assert: return addresses (addr2line -f -e <build>/halo_guest.elf):%s", text);
	error(_error_log, "EXCEPTION assert in %s,#%ld: %s (debug.fatal_asserts: the run ends; return addresses:%s)",
		file, line, information ? information : "<no reason given>", text);
	assert_report_summary();
	fflush(NULL);
	_exit(ASSERT_REPORT_EXIT_CODE);
}

/* at exit, when anything failed: each place, how many times */
static void assert_report_summary(
	void)
{
	unsigned long failed, warned;
	long places = 0;
	long index;

	assert_report_counts(&failed, &warned);
	if (!failed && !warned)
		return;
	for (index = 0; index < MAXIMUM_ASSERT_REPORT_PLACES; index++)
		places += assert_report.places[index].file != NULL;
	platform_log("asserts: %lu failed, %lu warnings, at %ld places%s", failed, warned, places,
		assert_report.unplaced ? " (and places the table had no room for)" : "");
	for (index = 0; index < MAXIMUM_ASSERT_REPORT_PLACES; index++)
	{
		struct assert_report_place const *place = &assert_report.places[index];

		if (place->file)
		{
			platform_log("asserts: %s %lu %s:%ld: %s", place->fatal ? "assert" : "warn", place->count,
				place->file, place->line, place->information);
		}
	}
}
