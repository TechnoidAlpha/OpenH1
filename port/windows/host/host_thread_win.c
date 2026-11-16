/*
HOST_THREAD_WIN.C

Threads that run guest code, on Windows (host.h's interface: the Linux and
macOS hosts' is port/android/host/host_thread.c).

Guest (x32) code keeps stack addresses in 32-bit registers, so every thread
that runs it needs its stack in guest memory, below 4 GB, which CreateThread
cannot place. Each such thread is created with a small stack of its own and
moves at once onto one in guest memory (host_run_on_stack,
host_enter_win.S), with its stack limits in its TEB moved too, and back
when it is done, before it frees that stack and ends. The game's main
thread is the process's, moved the same way (host_main_win.c).

A low stack is reserved in a pool and committed whole (the guest's code,
built for Linux, does not probe its stack as Windows's growth needs), with
guard pages under it, then a page that is never committed: a stack
overflow is a STATUS_STACK_OVERFLOW (the guard pages) with room left to
report it, then an access violation.

The guest's thread pointer (its musl struct pthread) is kept per thread in
the host's thread-local storage, as on Linux.
*/

#include "host_win.h"

#include <windows.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>

/* the never-committed page, and the guard pages above it */
#define NO_ACCESS_SIZE 0x1000
#define GUARD_SIZE 0x4000
/* each thread's own stack, before it moves */
#define NATIVE_STACK_SIZE (256 * 1024)

static _Thread_local uint32_t guest_tp;

uint32_t host_get_tp(void)
{
	return guest_tp;
}

void host_set_tp(uint32_t thread)
{
	guest_tp = thread;
}

/* ---------- low stacks */

int host_run_on_low_stack(void (*function)(void *), void *argument, size_t stack_size)
{
	size_t total;
	char *mapping, *limit, *top;

	stack_size = (stack_size + 0xffff) & ~(size_t)0xffff;
	total = NO_ACCESS_SIZE + GUARD_SIZE + stack_size;
	/* (reserved; committed below) */
	mapping = host_low_map(total, PROT_NONE);
	if (!mapping)
		return ENOMEM;
	limit = mapping + NO_ACCESS_SIZE + GUARD_SIZE;
	top = mapping + total;
	if (VirtualAlloc(limit, stack_size, MEM_COMMIT, PAGE_READWRITE) != limit ||
		VirtualAlloc(mapping + NO_ACCESS_SIZE, GUARD_SIZE, MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD) !=
			mapping + NO_ACCESS_SIZE)
	{
		host_low_unmap(mapping, total);
		return ENOMEM;
	}
	host_run_on_stack(function, argument, top, limit, mapping);
	host_low_unmap(mapping, total);
	return 0;
}

static int on_guest_stack(void)
{
	return host_in_guest((uintptr_t)__builtin_frame_address(0));
}

/* ---------- calling into the guest

The guest's functions are called through function pointers of its calling
convention (System V's), with their arguments as 64-bit values,
zero-extended, as on Linux (port/android/host/host_thread.c); the compiler
saves around the call what Windows's convention keeps and System V's does
not (rsi, rdi, xmm6-15). The result is the guest's 32-bit one (eax). */

typedef uint32_t (HOST_GUEST_ABI *guest_function)(uint64_t, uint64_t, uint64_t, uint64_t);

static uint32_t enter_guest(uint32_t function, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	return ((guest_function)(uintptr_t)function)(a, b, c, d);
}

uint32_t host_call_guest(uint32_t function, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	if (!on_guest_stack())
		host_fatal("guest code called on a thread without a guest stack");
	if (!guest_tp)
		enter_guest(host_image.header->thread_attach, 0, 0, 0, 0);
	return enter_guest(function, a, b, c, d);
}

void host_run_guest_main(uint32_t boot)
{
	enter_guest(host_image.header->start, boot, 0, 0, 0);
	host_fatal("the guest returned from __guest_start");
}

/* ---------- guest threads */

struct thread_start
{
	void *(*function)(void *);
	void *argument;
	size_t stack_size;
};

static void thread_on_stack(void *context)
{
	struct thread_start *start = context;

	start->function(start->argument);
	/* (a thread the guest made has let its struct pthread go by now; one
	of the host's that ran guest code, the audio thread, has it still) */
	if (guest_tp)
		enter_guest(host_image.header->thread_detach, 0, 0, 0, 0);
	guest_tp = 0;
}

static DWORD WINAPI thread_main(LPVOID context)
{
	struct thread_start start = *(struct thread_start *)context;
	int error;

	free(context);
	host_debug_thread_started();
	error = host_run_on_low_stack(thread_on_stack, &start, start.stack_size);
	host_debug_thread_exited();
	if (error)
		host_fatal("no guest memory for a thread's stack (%zu bytes)", start.stack_size);
	return 0;
}

int host_native_thread_create(void *(*function)(void *), void *argument, size_t stack_size)
{
	struct thread_start *start = malloc(sizeof(*start));
	HANDLE thread;

	if (!start)
		return ENOMEM;
	start->function = function;
	start->argument = argument;
	start->stack_size = stack_size;
	thread = CreateThread(NULL, NATIVE_STACK_SIZE, thread_main, start, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
	if (!thread)
	{
		free(start);
		return EAGAIN;
	}
	CloseHandle(thread);
	return 0;
}

static void *guest_thread_main(void *guest_thread)
{
	host_call_guest(host_image.header->thread_start, (uint32_t)(uintptr_t)guest_thread, 0, 0, 0);
	return NULL;
}

int host_thread_create(uint32_t guest_thread, uint32_t stack_size)
{
	return host_native_thread_create(guest_thread_main, (void *)(uintptr_t)guest_thread, stack_size);
}

/* (host.h: the signal handlers' stacks, which Windows's exception dispatch
does without) */
int host_signal_stack_install(void)
{
	return 0;
}

void host_signal_stack_remove(void)
{
}

/* ---------- the mix thread's scheduling (host_sdl.c's audio_thread)

The Multimedia Class Scheduler (MMCSS) runs a thread of its "Pro Audio"
task in the real-time priority band for as long as the task holds it, as
Windows's own audio engine runs: above THREAD_PRIORITY_TIME_CRITICAL in a
normal process, and not starved by a busy computer. avrt.dll is loaded when
it is first asked for. The task's handle (NULL: none, the thread keeps the
priority it has) */

typedef HANDLE (WINAPI *mmcss_begin_function)(const wchar_t *task, DWORD *index);
typedef BOOL (WINAPI *mmcss_end_function)(HANDLE task);
static mmcss_begin_function mmcss_begin;
static mmcss_end_function mmcss_end;
static INIT_ONCE mmcss_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK mmcss_load(INIT_ONCE *once, void *parameter, void **context)
{
	HMODULE library = LoadLibraryExW(L"avrt.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

	(void)once;
	(void)parameter;
	(void)context;
	if (library)
	{
		mmcss_begin = (mmcss_begin_function)(void *)GetProcAddress(library, "AvSetMmThreadCharacteristicsW");
		mmcss_end = (mmcss_end_function)(void *)GetProcAddress(library, "AvRevertMmThreadCharacteristics");
	}
	return TRUE;
}

void *host_thread_pro_audio_begin(void)
{
	static LONG logged;
	DWORD index = 0;
	HANDLE task = NULL;
	DWORD error = 0;

	InitOnceExecuteOnce(&mmcss_once, mmcss_load, NULL, NULL);
	if (mmcss_begin && mmcss_end)
	{
		task = mmcss_begin(L"Pro Audio", &index);
		if (!task)
			error = GetLastError();
	}
	if (!InterlockedExchange(&logged, 1))
	{
		if (task)
			host_logf(HOST_LOG_INFO, "sound: the mix thread is MMCSS's \"Pro Audio\" task (index %lu)",
				(unsigned long)index);
		else if (mmcss_begin && mmcss_end)
			host_logf(HOST_LOG_INFO, "sound: no MMCSS task for the mix thread (error %lu): it keeps its priority",
				(unsigned long)error);
		else
			host_logf(HOST_LOG_INFO, "sound: no MMCSS (avrt.dll) for the mix thread: it keeps its priority");
	}
	return task;
}

void host_thread_pro_audio_end(void *task)
{
	if (task && mmcss_end)
		mmcss_end(task);
}
