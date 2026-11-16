/* Opt-in loading diagnostics. Times share CLOCK_MONOTONIC with the guest.
Submission and swap events are not proof of physical display presentation. */
#ifndef HOST_LOAD_TRACE_H
#define HOST_LOAD_TRACE_H
#include "host.h"
#include <pthread.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* an environment variable's switch, read once (a getenv a frame was 0.9% of
the render thread on Windows, whose getenv locks and scans the environment:
its callers are per present and per wait); -1 until read */
static inline int host_load_trace_switch(const char *variable, int *cached)
{
	int value = __atomic_load_n(cached, __ATOMIC_RELAXED);

	if (value < 0)
	{
		const char *text = getenv(variable);

		value = text && *text && strcmp(text, "0") && strcmp(text, "false");
		__atomic_store_n(cached, value, __ATOMIC_RELAXED);
	}
	return value;
}

static inline int host_load_trace_enabled(void)
{
	static int enabled = -1;

	return host_load_trace_switch("HALO_LOAD_TRACE", &enabled);
}

static inline double host_load_trace_clock(clockid_t clock)
{
	struct timespec value;

	return clock_gettime(clock, &value) == 0 ? (double)value.tv_sec + value.tv_nsec / 1e9 : -1.0;
}

static inline int host_load_trace_detail_enabled(void)
{
	static int detail = -1;

	return host_load_trace_enabled() && host_load_trace_switch("HALO_LOAD_TRACE_DETAIL", &detail);
}

static inline void host_load_trace(const char *phase, const char *format, ...)
{
	double wall, thread, process;
	char detail[256];
	va_list arguments;

	if (!host_load_trace_enabled())
		return;
	wall = host_load_trace_clock(CLOCK_MONOTONIC);
	thread = host_load_trace_clock(CLOCK_THREAD_CPUTIME_ID);
	process = host_load_trace_clock(CLOCK_PROCESS_CPUTIME_ID);
	va_start(arguments, format);
	vsnprintf(detail, sizeof(detail), format, arguments);
	va_end(arguments);
	/* pthread identity is local to this host trace, not an SDL thread ID. */
	host_logf(HOST_LOG_INFO, "LOAD phase=%s wall=%.6f thread=%.6f process=%.6f owner=host:%llx detail=%s",
		phase, wall, thread, process, (unsigned long long)(uintptr_t)pthread_self(), detail);
}
#endif
