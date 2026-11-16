/*
HOST_PERFORMANCE.H

Android's performance hints for the game's threads (host_performance.c);
nothing elsewhere.
*/

#ifndef HOST_PERFORMANCE_H
#define HOST_PERFORMANCE_H

#include <stdint.h>

#ifdef __ANDROID__
/* the calling thread is one a frame waits for (once a thread) */
void host_performance_thread(void);
/* a frame's work, begun at start (CLOCK_MONOTONIC), took work nanoseconds
of the CPU's and gpu of the GPU's (0 not known); its target, the display's
refresh, is period (0 unchanged) */
void host_performance_frame(uint64_t start, uint64_t work, uint64_t gpu, uint64_t period);
#else
#define host_performance_thread() ((void)0)
#define host_performance_frame(start, work, gpu, period) ((void)(start), (void)(work), (void)(gpu), (void)(period))
#endif

#endif
