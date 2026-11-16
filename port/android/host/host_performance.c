/*
HOST_PERFORMANCE.C

Android's performance hints (APerformanceHint, Android 13 and later) for
the threads a frame waits for: the game's and the render thread's. Without
them a phone's scheduler sees threads that sleep between frames and runs
them on its slowest clocks, so that the frames come late; with them it is
told each frame's target, the display's refresh, and how long the frame's
work took, from the game reading its input to the render thread's frame
handed to the GPU, and sets the clocks (and the cores) that keep it in
time. The functions are looked up in libandroid.so, as the app runs on
Android 9 too, where they are not; there they do nothing.
*/

#ifdef __ANDROID__

#include "host.h"
#include "host_performance.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define PERFORMANCE_THREADS 4
/* (the target until the display gives its refresh) */
#define PERFORMANCE_DEFAULT_TARGET_NS 16666667ll

struct performance_manager;
struct performance_session;
struct performance_work;

static struct
{
	pthread_mutex_t lock;
	int looked_up;
	struct performance_manager *(*get_manager)(void);
	struct performance_session *(*create_session)(struct performance_manager *manager, const int32_t *threads,
		size_t count, int64_t target);
	int (*update_target)(struct performance_session *session, int64_t target);
	int (*report_actual)(struct performance_session *session, int64_t actual);
	void (*close_session)(struct performance_session *session);
	/* (Android 14 and later) */
	int (*set_threads)(struct performance_session *session, const pid_t *threads, size_t count);
	/* (Android 15 and later: a frame's GPU time too, which sets the GPU's
	clock as well) */
	struct performance_work *(*work_create)(void);
	void (*work_release)(struct performance_work *work);
	void (*work_start)(struct performance_work *work, int64_t start);
	void (*work_total)(struct performance_work *work, int64_t total);
	void (*work_cpu)(struct performance_work *work, int64_t cpu);
	void (*work_gpu)(struct performance_work *work, int64_t gpu);
	int (*report_work)(struct performance_session *session, struct performance_work *work);
	struct performance_work *work;
	struct performance_manager *manager;
	struct performance_session *session;
	int32_t threads[PERFORMANCE_THREADS];
	size_t thread_count;
	int64_t target;
	int failed;
} performance = { PTHREAD_MUTEX_INITIALIZER };

/* (performance.lock held) */
static int performance_look_up(void)
{
	void *library;

	if (performance.looked_up)
		return performance.manager != NULL;
	performance.looked_up = 1;
	library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
	if (!library)
		return 0;
	performance.get_manager = (struct performance_manager *(*)(void))dlsym(library, "APerformanceHint_getManager");
	performance.create_session = (struct performance_session *(*)(struct performance_manager *, const int32_t *,
		size_t, int64_t))dlsym(library, "APerformanceHint_createSession");
	performance.update_target = (int (*)(struct performance_session *, int64_t))dlsym(library,
		"APerformanceHint_updateTargetWorkDuration");
	performance.report_actual = (int (*)(struct performance_session *, int64_t))dlsym(library,
		"APerformanceHint_reportActualWorkDuration");
	performance.close_session = (void (*)(struct performance_session *))dlsym(library,
		"APerformanceHint_closeSession");
	performance.set_threads = (int (*)(struct performance_session *, const pid_t *, size_t))dlsym(library,
		"APerformanceHint_setThreads");
	performance.work_create = (struct performance_work *(*)(void))dlsym(library, "AWorkDuration_create");
	performance.work_release = (void (*)(struct performance_work *))dlsym(library, "AWorkDuration_release");
	performance.work_start = (void (*)(struct performance_work *, int64_t))dlsym(library,
		"AWorkDuration_setWorkPeriodStartTimestampNanos");
	performance.work_total = (void (*)(struct performance_work *, int64_t))dlsym(library,
		"AWorkDuration_setActualTotalDurationNanos");
	performance.work_cpu = (void (*)(struct performance_work *, int64_t))dlsym(library,
		"AWorkDuration_setActualCpuDurationNanos");
	performance.work_gpu = (void (*)(struct performance_work *, int64_t))dlsym(library,
		"AWorkDuration_setActualGpuDurationNanos");
	performance.report_work = (int (*)(struct performance_session *, struct performance_work *))dlsym(library,
		"APerformanceHint_reportActualWorkDuration2");
	if (performance.work_create && performance.work_release && performance.work_start && performance.work_total &&
		performance.work_cpu && performance.work_gpu && performance.report_work)
		performance.work = performance.work_create();
	if (!performance.get_manager || !performance.create_session || !performance.update_target ||
		!performance.report_actual || !performance.close_session)
	{
		host_logf(HOST_LOG_INFO, "performance hints: not on this version of Android");
		return 0;
	}
	performance.manager = performance.get_manager();
	if (!performance.manager)
		host_logf(HOST_LOG_INFO, "performance hints: not on this device");
	return performance.manager != NULL;
}

/* (performance.lock held) a session of the threads, or its threads changed */
static void performance_session_threads(void)
{
	if (performance.session && performance.set_threads &&
		performance.set_threads(performance.session, performance.threads, performance.thread_count) == 0)
	{
		host_logf(HOST_LOG_INFO, "performance hints: the session's threads, %u", (unsigned)performance.thread_count);
		return;
	}
	if (performance.session)
		performance.close_session(performance.session);
	performance.session = performance.create_session(performance.manager, performance.threads,
		performance.thread_count, performance.target);
	if (!performance.session)
	{
		performance.failed = 1;
		host_logf(HOST_LOG_WARN, "performance hints: no session for the game's threads");
		return;
	}
	host_logf(HOST_LOG_INFO, "performance hints: a session of %u threads, %.2f ms a frame%s",
		(unsigned)performance.thread_count, (double)performance.target / 1e6,
		performance.work ? ", with the GPU's time" : "");
}

void host_performance_thread(void)
{
	static __thread int joined;
	int32_t thread = (int32_t)gettid();

	if (joined)
		return;
	joined = 1;
	pthread_mutex_lock(&performance.lock);
	if (!performance.target)
		performance.target = PERFORMANCE_DEFAULT_TARGET_NS;
	if (!performance.failed && performance.thread_count < PERFORMANCE_THREADS && performance_look_up())
	{
		performance.threads[performance.thread_count++] = thread;
		performance_session_threads();
	}
	pthread_mutex_unlock(&performance.lock);
}

void host_performance_frame(uint64_t start, uint64_t work, uint64_t gpu, uint64_t period)
{
	pthread_mutex_lock(&performance.lock);
	if (performance.session)
	{
		if (period && (int64_t)period != performance.target)
		{
			performance.target = (int64_t)period;
			performance.update_target(performance.session, performance.target);
		}
		/* (the frame's time, the longer of the CPU's and the GPU's, which
		work on frames one after the other: each is to fit in a refresh) */
		if (work && gpu && start && performance.work)
		{
			performance.work_start(performance.work, (int64_t)start);
			performance.work_total(performance.work, (int64_t)(work > gpu ? work : gpu));
			performance.work_cpu(performance.work, (int64_t)work);
			performance.work_gpu(performance.work, (int64_t)gpu);
			performance.report_work(performance.session, performance.work);
		}
		else if (work)
			performance.report_actual(performance.session, (int64_t)work);
	}
	pthread_mutex_unlock(&performance.lock);
}

#else

/* (no translation unit is empty) */
typedef int host_performance_android_only;

#endif
