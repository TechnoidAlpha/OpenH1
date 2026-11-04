/*
XGPU_VULKAN_PACING.C

display.low_latency with vsync on (xgpu_vulkan_pacing.h). With FIFO the
display shows a frame at each refresh and the frames presented before it
wait in the swapchain's queue: a game left to run starts each frame as soon
as an image is free, its input read then, and so shows it two or three
refreshes later. Paced, the game starts a frame only while at most one of
its frames waits for the display, and not before the time it takes ahead of
the refresh it is for (frame_pacing.h, host_render_stream.c's display_wait):
its input is read as late as the frame can still make that refresh.

This file tells the command stream when the display took each frame and
when it showed it. Each present gets a number (VK_KHR_present_id, and
VK_GOOGLE_display_timing's presentID); a thread of its own waits for the
presents in order. First it waits for the frame's work (the backend's
timeline): the time from the frame's input to then is a cost the stream
budgets for (host_render_stream_frame_finished). Then for the present
(vkWaitForPresentKHR): it comes back when the display has taken the frame,
which is the time the next frames are paced by. That is the refresh itself
where the driver flips at it (X11, Wayland), or the compositor's own latch,
some milliseconds before it: gamescope's WSI layer, on the Steam Deck,
comes back 4.6 ms before the refresh (measured headless at 60 Hz), the time
gamescope takes the frame for it. Where the display says when it showed
each frame and how far apart its refreshes are (VK_GOOGLE_display_timing:
gamescope's layer, Android), those are the frame's time on screen and the
period; without present wait (Android) the thread asks display timing every
millisecond, and its time is the latch too. Else the period is the window's
display mode's; times the refreshes each frame is shown for (battery
saver's interval). All of it goes to host_render_stream_frame_displayed,
which counts the frame taken and learns the margin by the frames that
missed the refresh they were started for.

Presents that never come (the window covered, the swapchain made again, a
present that failed) are taken as shown at once (FRAME_WAIT_MOST_NS at
most), so the game never stops for them.

The game is paced only once the display has said when it took a present:
the first frames are presented as they come, so that a display that says
nothing until it has shown one is still shown them. A display that says
nothing of SILENT_PRESENTS presents in a row, or a swapchain that gives no
image for seconds (xgpu_vulkan.c's acquire), is no longer waited for, and
the game no longer paced (on a Steam Deck in Game Mode, build 940 showed no
frame at all with vsync on and pacing).

The environment's HALO_VULKAN_FRAME_LOG names a file to write a line to for
each frame, paced or not (CLOCK_MONOTONIC nanoseconds):

    frame <input> <queued> <finished> <shown> <refreshes> <paced> <earliest> <margin> <returned>

input the frame's input read (the stream's FRAME_END), queued its present's
return, finished its work done on the GPU (0 not waited for), shown when it
reached the screen (display timing's; else present wait's return; 0 not
known), refreshes those since the frame before reached it (0 not known;
more than the frames' interval: refreshes that showed a frame again), paced
1 if the game was paced, display timing's earliestPresentTime and
presentMargin (0 not known), present wait's return (0 not known).
HALO_VULKAN_STATS (or debug.gpu_stats) adds a line each 600 frames: the
refreshes that showed a frame again, the frames that missed their refresh,
the margin and the input to screen time.
*/

#include "xgpu_vulkan_pacing.h"
#include "host.h"
#include "host_render_stream.h"
#include "../src/frame_pacing.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* presents in order, waited for */
#define QUEUE 64
/* the longest a present is waited for before it is taken as shown: less
than the command stream's own longest wait for the display
(FRAME_PACING_WAIT_MAXIMUM_NS), so that it never counts frames taken on
its own, which this file would then count again */
#define FRAME_WAIT_MOST_NS 80000000ull
/* each wait's part, so that a swapchain made again is let go of soon */
#define WAIT_PART_NS 4000000ull
/* display timing alone: how often it is asked */
#define POLL_NS 1000000ull
/* display timing's times kept by present number */
#define TIMINGS 64
/* presents in a row the display said nothing of (FRAME_WAIT_MOST_NS each)
before it is taken to say nothing at all: no longer waited for, and the
game no longer paced */
#define SILENT_PRESENTS 8
#define STATISTICS_FRAMES 600

struct entry
{
	uint64_t id, input, queued, serial, finished;
	/* display timing's earliest time it could have been shown and how early
	it came for the time it was (0: not known) */
	uint64_t earliest, margin;
	/* when present wait came back for it (0: not waited for) */
	uint64_t returned;
	VkSwapchainKHR swapchain;
	/* the first present of a frame; counted by the stream (paced as it was
	presented) */
	int frame, counted;
};

static struct
{
	pthread_mutex_t lock;
	pthread_cond_t work, idle;
	pthread_t thread;
	/* the device has a way to know when a present is shown; the thread
	runs; it is asked to stop */
	int available, started, stopping;
	struct xgpu_vulkan_pacing_device device;
	PFN_vkWaitForPresentKHR wait_for_present;
	PFN_vkWaitSemaphores wait_semaphores;
	PFN_vkGetPastPresentationTimingGOOGLE past_timing;
	PFN_vkGetRefreshCycleDurationGOOGLE refresh_cycle;
	/* the swapchain presented to; the thread waits on it, unlocked */
	VkSwapchainKHR swapchain;
	int waiting;
	struct entry queue[QUEUE];
	uint32_t first, count;
	uint64_t next_id;
	/* paced now (the stream told); pacing asked for (display.low_latency
	with FIFO), which begins only once the display has said when it took a
	present (proven); the display has said nothing of SILENT_PRESENTS in a
	row (silent_run): its presents are no longer waited for; the swap
	interval; the refresh period by the window's mode and by display timing
	(0: none) */
	int paced, wanted, proven, silent, silent_run, interval;
	uint64_t mode_period, refresh;
	/* display timing's times, by present number */
	struct
	{
		uint64_t id, shown, earliest, margin;
	} timings[TIMINGS];
	/* the thread's: the last frame's time on screen; the frame log, the
	statistics' counts */
	uint64_t last_shown;
	FILE *log;
	int statistics;
	uint32_t frames, repeated, unknown;
	float input_to_screen[STATISTICS_FRAMES];
	uint32_t input_to_screen_count;
} pacing =
{
	.lock = PTHREAD_MUTEX_INITIALIZER,
	.work = PTHREAD_COND_INITIALIZER,
	.idle = PTHREAD_COND_INITIALIZER,
};

static uint64_t now_ns(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void sleep_ns(uint64_t nanoseconds)
{
	struct timespec span;

	span.tv_sec = (time_t)(nanoseconds / 1000000000ull);
	span.tv_nsec = (long)(nanoseconds % 1000000000ull);
	nanosleep(&span, NULL);
}

/* (lock held) the period the stream paces by: a refresh's (display
timing's, else the mode's) times the refreshes each frame is shown for */
static uint64_t period_now(void)
{
	uint64_t refresh = pacing.refresh ? pacing.refresh : pacing.mode_period;

	return refresh * (uint64_t)(pacing.interval > 1 ? pacing.interval : 1);
}

/* (lock held) display timing's times so far, kept by present number; and
the refresh cycle now (gamescope's changes with its frame limit) */
static void timings_take(void)
{
	VkPastPresentationTimingGOOGLE timings[16];
	VkRefreshCycleDurationGOOGLE cycle;
	uint32_t count, index, rounds = 0;
	VkResult result;

	if (!pacing.past_timing || !pacing.swapchain)
		return;
	/* (TIMINGS at most a call: gamescope's layer on the Steam Deck answers
	VK_INCOMPLETE with 16 times at every call, without end. Asked again until
	it said VK_SUCCESS, this thread never let go of the lock, and the next
	present, which takes it, never came: the Deck showed no frame at all) */
	do
	{
		count = 16;
		result = pacing.past_timing(pacing.device.device, pacing.swapchain, &count, timings);
		if (result < 0)
			return;
		for (index = 0; index < count; index++)
		{
			uint64_t id = timings[index].presentID;

			pacing.timings[id % TIMINGS].id = id;
			pacing.timings[id % TIMINGS].shown = timings[index].actualPresentTime;
			pacing.timings[id % TIMINGS].earliest = timings[index].earliestPresentTime;
			pacing.timings[id % TIMINGS].margin = timings[index].presentMargin;
		}
	}
	while (result == VK_INCOMPLETE && ++rounds < TIMINGS / 16);
	if (pacing.refresh_cycle && pacing.refresh_cycle(pacing.device.device, pacing.swapchain, &cycle) >= 0 &&
		cycle.refreshDuration > 1000000ull && cycle.refreshDuration < 100000000ull)
		pacing.refresh = cycle.refreshDuration;
}

/* (lock held) display timing's time of a present (0: none yet); display
timing's numbers are 32 bits */
static uint64_t timing_of(uint64_t id)
{
	uint32_t slot = (uint32_t)(id & 0xffffffffu) % TIMINGS;

	return pacing.timings[slot].id == (id & 0xffffffffu) ? pacing.timings[slot].shown : 0;
}

/* (lock held) whether display timing has given a present after this one
(FIFO shows them in order: this one has no time of its own) */
static int timing_passed(uint64_t id)
{
	uint32_t index;

	for (index = 0; index < TIMINGS; index++)
	{
		if (pacing.timings[index].id > (id & 0xffffffffu))
			return 1;
	}
	return 0;
}

static int float_compare(const void *a, const void *b)
{
	float x = *(const float *)a, y = *(const float *)b;

	return x < y ? -1 : x > y;
}

/* (the thread's, unlocked) a present done: shown then (0: not known) */
static void present_done(const struct entry *entry, uint64_t shown, uint64_t period, int paced)
{
	uint64_t refresh = period / (uint64_t)(pacing.interval > 1 ? pacing.interval : 1);
	uint32_t refreshes = 0;

	if (!entry->frame)
		return;
	if (entry->counted && paced)
		host_render_stream_frame_displayed(entry->input, entry->finished > 1 ? entry->finished : 0, entry->returned,
			shown, period);
	if (shown && pacing.last_shown && shown > pacing.last_shown && refresh)
	{
		refreshes = (uint32_t)((shown - pacing.last_shown + refresh / 2) / refresh);
		if (refreshes > (uint32_t)(pacing.interval > 1 ? pacing.interval : 1))
			pacing.repeated += refreshes - (uint32_t)(pacing.interval > 1 ? pacing.interval : 1);
	}
	pacing.last_shown = shown;
	if (!shown)
		pacing.unknown++;
	if (pacing.log)
		fprintf(pacing.log, "frame %llu %llu %llu %llu %u %d %llu %llu %llu\n", (unsigned long long)entry->input,
			(unsigned long long)entry->queued, (unsigned long long)entry->finished, (unsigned long long)shown,
			refreshes, entry->counted && paced, (unsigned long long)entry->earliest, (unsigned long long)entry->margin,
			(unsigned long long)entry->returned);
	if (shown && entry->input && shown > entry->input && pacing.input_to_screen_count < STATISTICS_FRAMES)
		pacing.input_to_screen[pacing.input_to_screen_count++] = (float)((double)(shown - entry->input) / 1e6);
	if (++pacing.frames < STATISTICS_FRAMES)
		return;
	if (pacing.statistics)
	{
		struct host_render_pacing_statistics statistics;
		float median = 0.0f, high = 0.0f;

		host_render_stream_pacing_statistics(&statistics);
		if (pacing.input_to_screen_count)
		{
			qsort(pacing.input_to_screen, pacing.input_to_screen_count, sizeof(float), float_compare);
			median = pacing.input_to_screen[pacing.input_to_screen_count / 2];
			high = pacing.input_to_screen[pacing.input_to_screen_count * 95 / 100];
		}
		host_logf(HOST_LOG_INFO, "vulkan: pacing %s: %u frames, %u refreshes showed a frame again, %u times not "
			"known; %u shown at their refresh, %u after it (%u ran long), %u started late; margin %.2f ms, "
			"budget %.2f ms; input to screen %.1f / %.1f ms (median / 95th percentile), refresh %.3f ms",
			paced ? "on" : "off", pacing.frames, pacing.repeated, pacing.unknown, statistics.kept, statistics.missed,
			statistics.overran, statistics.late_starts, (double)statistics.margin / 1e6,
			(double)statistics.budget / 1e6, (double)median, (double)high, (double)refresh / 1e6);
	}
	if (pacing.log)
		fflush(pacing.log);
	pacing.frames = pacing.repeated = pacing.unknown = 0;
	pacing.input_to_screen_count = 0;
}

static void *pacing_thread(void *unused)
{
	(void)unused;
	pthread_mutex_lock(&pacing.lock);
	while (!pacing.stopping)
	{
		struct entry *entry, done;
		uint64_t shown = 0, period, now;
		int paced, finish, unpace;

		if (!pacing.count)
		{
			pthread_cond_wait(&pacing.work, &pacing.lock);
			continue;
		}
		entry = &pacing.queue[pacing.first];
		now = now_ns();
		/* (a present waited for too long: the display said nothing of it) */
		if (entry->id && !pacing.silent && entry->swapchain == pacing.swapchain && now - entry->queued >= FRAME_WAIT_MOST_NS &&
			++pacing.silent_run >= SILENT_PRESENTS)
		{
			pacing.silent = 1;
			host_logf(HOST_LOG_WARN, "vulkan: the display said nothing of %d presents in a row: they are no longer "
				"waited for, and the game's frames not paced by it", SILENT_PRESENTS);
		}
		/* (nothing presented, or to a swapchain let go of, or waited for
		too long, or a display that says nothing: taken as shown, its time
		not known) */
		if (entry->id && !pacing.silent && entry->swapchain == pacing.swapchain &&
			now - entry->queued < FRAME_WAIT_MOST_NS)
		{
			VkSwapchainKHR swapchain = entry->swapchain;
			uint64_t id = entry->id;
			VkResult result = VK_TIMEOUT;

			/* (its work first: the frame's cost) */
			finish = entry->frame && entry->serial && !entry->finished && pacing.wait_semaphores;
			pacing.waiting = 1;
			pthread_mutex_unlock(&pacing.lock);
			if (finish)
			{
				VkSemaphoreWaitInfo info;
				uint64_t value = entry->serial;

				memset(&info, 0, sizeof(info));
				info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
				info.semaphoreCount = 1;
				info.pSemaphores = &pacing.device.timeline;
				info.pValues = &value;
				if (pacing.wait_semaphores(pacing.device.device, &info, FRAME_WAIT_MOST_NS) == VK_SUCCESS)
				{
					uint64_t finished = now_ns();

					entry->finished = finished;
					if (entry->counted && entry->input)
						host_render_stream_frame_finished(entry->input, finished);
				}
				else
				{
					entry->finished = 1;
				}
			}
			if (pacing.wait_for_present)
			{
				result = pacing.wait_for_present(pacing.device.device, swapchain, id, WAIT_PART_NS);
				if (result == VK_SUCCESS)
					entry->returned = now_ns();
			}
			else
			{
				sleep_ns(POLL_NS);
			}
			now = now_ns();
			pthread_mutex_lock(&pacing.lock);
			pacing.waiting = 0;
			pthread_cond_broadcast(&pacing.idle);
			if (pacing.stopping)
				break;
			if (entry->swapchain != pacing.swapchain)
				continue;
			/* (present wait: not yet) */
			if (pacing.wait_for_present && result == VK_TIMEOUT)
				continue;
			if (pacing.past_timing)
			{
				timings_take();
				shown = timing_of(id);
				/* (present wait back before display timing has the time: asked
				again for a little while) */
				if (!shown && result == VK_SUCCESS)
				{
					int tries;

					for (tries = 0; tries < 8 && !shown; tries++)
					{
						pthread_mutex_unlock(&pacing.lock);
						sleep_ns(250000);
						pthread_mutex_lock(&pacing.lock);
						if (entry->swapchain != pacing.swapchain)
							break;
						timings_take();
						shown = timing_of(id);
					}
					if (entry->swapchain != pacing.swapchain)
						continue;
				}
				if (shown)
				{
					entry->earliest = pacing.timings[(uint32_t)(id & 0xffffffffu) % TIMINGS].earliest;
					entry->margin = pacing.timings[(uint32_t)(id & 0xffffffffu) % TIMINGS].margin;
				}
				/* (display timing alone: not yet, unless a later present has
				its time) */
				else if (!pacing.wait_for_present && !timing_passed(id))
				{
					continue;
				}
			}
			/* (no time from the display: present wait's return, else not
			known: an error) */
			if (!shown && result == VK_SUCCESS)
				shown = entry->returned;
			/* (the display says when it takes the presents: the game may be
			paced by it, from the next present, xgpu_vulkan_pacing_present) */
			if (shown)
			{
				pacing.silent_run = 0;
				pacing.proven = 1;
			}
		}
		done = *entry;
		pacing.first = (pacing.first + 1) % QUEUE;
		pacing.count--;
		period = period_now();
		paced = pacing.paced;
		/* (a display gone silent: the game no longer waits for it) */
		unpace = pacing.silent && pacing.paced;
		if (unpace)
			pacing.paced = paced = 0;
		pthread_mutex_unlock(&pacing.lock);
		if (unpace)
			host_render_stream_display_paced(0);
		present_done(&done, shown, period, paced);
		pthread_mutex_lock(&pacing.lock);
	}
	pthread_mutex_unlock(&pacing.lock);
	return NULL;
}

/* the thread started (render thread): at the first frame paced, or at the
start for the frame log or the statistics */
static int thread_start(void)
{
	const char *log = getenv("HALO_VULKAN_FRAME_LOG");

	if (pacing.started)
		return 1;
	if (!pacing.available)
		return 0;
	pacing.statistics = pacing.device.statistics || getenv("HALO_VULKAN_STATS") != NULL;
	if (log && *log && !pacing.log)
	{
		pacing.log = fopen(log, "w");
		if (!pacing.log)
			host_logf(HOST_LOG_WARN, "vulkan: HALO_VULKAN_FRAME_LOG: cannot write %s", log);
	}
	pacing.stopping = 0;
	pacing.proven = pacing.silent = pacing.silent_run = 0;
	pacing.first = pacing.count = 0;
	pacing.last_shown = 0;
	memset(pacing.timings, 0, sizeof(pacing.timings));
	if (pthread_create(&pacing.thread, NULL, pacing_thread, NULL))
		return 0;
	pacing.started = 1;
	host_logf(HOST_LOG_INFO, "vulkan: presents waited for by %s", pacing.wait_for_present && pacing.past_timing ?
		"present wait, timed by display timing" : pacing.wait_for_present ? "present wait" : "display timing");
	return 1;
}

int xgpu_vulkan_pacing_start(const struct xgpu_vulkan_pacing_device *device)
{
	const char *log = getenv("HALO_VULKAN_FRAME_LOG");

	xgpu_vulkan_pacing_stop();
	pacing.device = *device;
	pacing.wait_for_present = device->present_wait ?
		(PFN_vkWaitForPresentKHR)device->get_device_proc_addr(device->device, "vkWaitForPresentKHR") : NULL;
	pacing.wait_semaphores = (PFN_vkWaitSemaphores)device->get_device_proc_addr(device->device, "vkWaitSemaphores");
	pacing.past_timing = device->display_timing ? (PFN_vkGetPastPresentationTimingGOOGLE)device->get_device_proc_addr(
		device->device, "vkGetPastPresentationTimingGOOGLE") : NULL;
	pacing.refresh_cycle = device->display_timing ? (PFN_vkGetRefreshCycleDurationGOOGLE)device->get_device_proc_addr(
		device->device, "vkGetRefreshCycleDurationGOOGLE") : NULL;
	if (!pacing.past_timing || !pacing.refresh_cycle)
		pacing.past_timing = NULL, pacing.refresh_cycle = NULL;
	pacing.available = pacing.wait_for_present || pacing.past_timing;
	if (pacing.available && ((log && *log) || device->statistics || getenv("HALO_VULKAN_STATS")))
		thread_start();
	return pacing.available;
}

void xgpu_vulkan_pacing_stop(void)
{
	pacing.available = 0;
	if (!pacing.started)
		return;
	if (pacing.paced)
		host_render_stream_display_paced(0);
	pthread_mutex_lock(&pacing.lock);
	pacing.stopping = 1;
	pacing.paced = 0;
	pthread_cond_broadcast(&pacing.work);
	pthread_mutex_unlock(&pacing.lock);
	pthread_join(pacing.thread, NULL);
	pacing.started = 0;
	pacing.swapchain = VK_NULL_HANDLE;
	if (pacing.log)
	{
		fclose(pacing.log);
		pacing.log = NULL;
	}
}

/* (render thread) the game paced by the display, or not: the stream told */
static int pacing_set(int paced)
{
	if (paced == pacing.paced)
		return paced;
	/* (the frames played so far taken, the next one this file's to say:
	before its present) */
	if (paced)
		host_render_stream_frames_taken_reset();
	pthread_mutex_lock(&pacing.lock);
	pacing.paced = paced;
	pthread_mutex_unlock(&pacing.lock);
	host_render_stream_display_paced(paced);
	host_logf(HOST_LOG_INFO, "vulkan: low latency %s: the game's frames %s", paced ? "on" : "off",
		paced ? "start in time for the refresh they are shown at" : "are not paced by the display");
	return paced;
}

int xgpu_vulkan_pacing_mode(int mode, int interval, int fifo, uint64_t period)
{
	/* (auto: on for the Linux desktop's, the Steam Deck's and Android's
	drivers; Windows' turns it on itself until its are measured) */
#if defined(_WIN32)
	int automatic = 0;
#else
	int automatic = 1;
#endif
	int wanted = pacing.available && fifo && (mode == 1 || (mode == 0 && automatic)) && thread_start();
	int ready;

	pthread_mutex_lock(&pacing.lock);
	pacing.interval = interval;
	if (period)
		pacing.mode_period = period;
	pacing.wanted = wanted;
	/* (not before the display has said when it took a present: until then
	the frames are presented as they come, so that a display that says
	nothing until it has shown one, or nothing at all, is still shown
	them) */
	ready = wanted && pacing.proven && !pacing.silent;
	pthread_mutex_unlock(&pacing.lock);
	return pacing_set(ready);
}

void xgpu_vulkan_pacing_swapchain(VkSwapchainKHR swapchain)
{
	pthread_mutex_lock(&pacing.lock);
	pacing.swapchain = swapchain;
	while (pacing.waiting)
		pthread_cond_wait(&pacing.idle, &pacing.lock);
	memset(pacing.timings, 0, sizeof(pacing.timings));
	pacing.refresh = 0;
	timings_take();
	pthread_cond_broadcast(&pacing.work);
	pthread_mutex_unlock(&pacing.lock);
}

int xgpu_vulkan_pacing_active(void)
{
	return pacing.paced;
}

VkResult xgpu_vulkan_pacing_present(PFN_vkQueuePresentKHR queue_present, VkQueue queue, VkPresentInfoKHR *info,
	int frame, uint64_t serial)
{
	VkPresentIdKHR ids;
	VkPresentTimesInfoGOOGLE times;
	VkPresentTimeGOOGLE time;
	const void *next = info->pNext;
	uint64_t id;
	VkResult result;
	int begin;

	if (!pacing.started)
		return queue_present(queue, info);
	pthread_mutex_lock(&pacing.lock);
	id = pacing.count < QUEUE ? ++pacing.next_id : 0;
	if (id && pacing.wait_for_present)
	{
		memset(&ids, 0, sizeof(ids));
		ids.sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR;
		ids.pNext = info->pNext;
		ids.swapchainCount = 1;
		ids.pPresentIds = &id;
		info->pNext = &ids;
	}
	if (id && pacing.past_timing)
	{
		memset(&times, 0, sizeof(times));
		time.presentID = (uint32_t)id;
		time.desiredPresentTime = 0;
		times.sType = VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE;
		times.pNext = info->pNext;
		times.swapchainCount = 1;
		times.pTimes = &time;
		info->pNext = &times;
	}
	/* (display timing's calls and the presents one at a time: they share
	the swapchain) */
	result = queue_present(queue, info);
	info->pNext = next;
	/* (pacing asked for, and the display has now said when it took a
	present: from the next frame, xgpu_vulkan_pacing_mode) */
	begin = pacing.wanted && pacing.proven && !pacing.silent && !pacing.paced;
	if (pacing.count < QUEUE && (id || frame))
	{
		struct entry *entry = &pacing.queue[(pacing.first + pacing.count++) % QUEUE];

		memset(entry, 0, sizeof(*entry));
		/* (a present that failed is not waited for) */
		entry->id = result >= 0 ? id : 0;
		entry->queued = now_ns();
		entry->serial = serial;
		entry->swapchain = pacing.swapchain;
		entry->frame = frame;
		entry->counted = frame && pacing.paced;
		entry->input = frame ? host_render_stream_frame_input() : 0;
		pthread_cond_broadcast(&pacing.work);
	}
	pthread_mutex_unlock(&pacing.lock);
	if (begin)
		pacing_set(1);
	return result;
}

VkResult xgpu_vulkan_pacing_acquire(PFN_vkAcquireNextImageKHR acquire, VkDevice device, VkSwapchainKHR swapchain,
	uint64_t timeout, VkSemaphore semaphore, uint32_t *index)
{
	VkResult result;

	if (!pacing.started)
		return acquire(device, swapchain, timeout, semaphore, VK_NULL_HANDLE, index);
	pthread_mutex_lock(&pacing.lock);
	result = acquire(device, swapchain, timeout, semaphore, VK_NULL_HANDLE, index);
	pthread_mutex_unlock(&pacing.lock);
	return result;
}

void xgpu_vulkan_pacing_unshown(void)
{
	struct entry *entry;

	if (!pacing.started)
		return;
	pthread_mutex_lock(&pacing.lock);
	if (pacing.count < QUEUE)
	{
		entry = &pacing.queue[(pacing.first + pacing.count++) % QUEUE];
		memset(entry, 0, sizeof(*entry));
		entry->queued = now_ns();
		entry->frame = 1;
		entry->counted = pacing.paced;
		entry->input = host_render_stream_frame_input();
		pthread_cond_broadcast(&pacing.work);
	}
	pthread_mutex_unlock(&pacing.lock);
}

void xgpu_vulkan_pacing_silence(void)
{
	int paced;

	if (!pacing.started)
		return;
	pthread_mutex_lock(&pacing.lock);
	if (!pacing.silent)
		host_logf(HOST_LOG_WARN, "vulkan: presents no longer waited for, and the game's frames not paced by the display");
	pacing.silent = 1;
	paced = pacing.paced;
	pthread_mutex_unlock(&pacing.lock);
	if (paced)
		pacing_set(0);
}
