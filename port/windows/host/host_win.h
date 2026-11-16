/*
HOST_WIN.H

Internals of the 64-bit Windows host (port/windows/README.md, "64-bit
Windows"): the files of port/windows/host, beside port/android/host's
portable ones (host.h), which it shares with the Linux and macOS hosts.

The guest is the x32 image of the 64-bit x86 Linux build, with the Windows
code paths (tools/windows_build.py). Its memory lies below 4 GB, as on
Linux (it has no base register): the host reserves that address space at
start-up (host_memory_win.c). Its threads run on stacks there
(host_thread_win.c, host_enter_win.S). It calls the host through thunks
from its calling convention (System V's) to Windows's (Microsoft's x64:
tools/android_imports.py --host-abi sysv), and the host calls it through
function pointers of System V's convention (HOST_GUEST_ABI). Its system
calls are Linux's, which host_syscall_win.c performs with Windows's
functions.
*/

#ifndef __HALO_WINDOWS_HOST_WIN_H
#define __HALO_WINDOWS_HOST_WIN_H

#include "host.h"

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

/* the guest's calling convention, for the host's calls of guest code */
#define HOST_GUEST_ABI __attribute__((sysv_abi))

/* ---------- memory (host_memory_win.c) */

/* the low 4 GB at start-up, before the host reserves anything, into the log
(and file, if not NULL), one line for each region */
void host_memory_log_low_map(FILE *file);
/* whether the fixed ranges are reserved for the guest (by this process at
start-up, or by the launcher, host_main_win.c) */
int host_memory_fixed_reserved(void);
/* commits size bytes (a whole number of pages) of the image's reserved
range past its end, read-write, for the host's own data that must lie
there (the unwinding data of host_memory_watch_win.c, which Windows finds
at 32-bit offsets from the image's base); NULL if there is no room */
void *host_memory_image_extra(size_t size);
/* (host_memory_watch.c, the guest's memory_watch_prepare_write) the pages
of a guest range the host is about to write into, unprotected */
void host_memory_watch_prepare_write(uint32_t address, uint32_t size);
/* Windows's page protection for a POSIX one (PROT_*) */
unsigned long host_windows_protection(int protection);

/* ---------- threads (host_thread_win.c, host_enter_win.S) */

/* calls function(argument) on the stack from stack_limit to stack_top (in
guest memory), with the thread's stack limits (its TEB's StackBase,
StackLimit and DeallocationStack) set to it, as SwitchToFiber does: Windows
and the C runtime check the stack pointer against them. The stack's first
frame has an exception handler, host_stack_exception, so that a crash on
it is reported. Returns once function does, on the thread's own stack with
its limits back. */
void host_run_on_stack(void (*function)(void *), void *argument, void *stack_top, void *stack_limit,
	void *deallocation);
/* calls function(argument) on a new stack of stack_size bytes in guest
memory (host_run_on_stack), freed when it returns; 0, or an errno value
if there is no memory for it */
int host_run_on_low_stack(void (*function)(void *), void *argument, size_t stack_size);

/* ---------- exceptions (host_memory_watch_win.c) */

/* the language-specific handler of each low stack's first frame
(host_enter_win.S): an exception no handler above took is a crash, which
it reports before the process ends (an EXCEPTION_DISPOSITION) */
int host_stack_exception(void *record, void *frame, void *context, void *dispatcher);
/* reports a crash: a minidump (host_crash_dump), then the exception, the
registers and the frames in the log; once (another thread's crash waits) */
void host_crash_report(void *exception_pointers);
/* makes the guest image's frames walkable by Windows's unwinder (its
crash reports and debuggers): a function table over the image whose
entries follow the guest's frame pointers (host_image must be loaded) */
void host_register_guest_frames(void);

/* ---------- crash reports (host_crash_win.c) */

/* a copy of halo.exe started as the crash reporter, or the uploader of the
reports left unsent ("--crash-report", "--crash-upload" on the command
line): does its work and returns 1 (main then ends); else 0 */
int host_crash_reporter(void);
/* abort() and the C runtime's invalid parameter checks raise exceptions
(reported as crashes); reports left unsent go, in the background */
void host_crash_install(void);
/* has the crash reporter write a minidump of this process, when reports
are on; 1 once it is written */
int host_crash_dump(void *exception_pointers);

/* ---------- the C runtime's descriptors (host_posix_win.c): its calls
without its invalid-parameter handler, so that a descriptor that is not open
fails (EBADF in errno; INVALID_HANDLE_VALUE, -1) rather than ending the game */

intptr_t host_crt_osfhandle(int fd);
int host_crt_close(int fd);
int host_crt_dup(int fd);
int host_crt_dup2(int from, int to);

/* ---------- time (host_posix_win.c): a FILETIME's value (100 ns intervals
since 1601) as seconds and nanoseconds since 1970, and back */

void host_unix_time_of_filetime(uint64_t filetime, int64_t *seconds, int64_t *nanoseconds);
uint64_t host_filetime_of_unix_time(int64_t seconds, int64_t nanoseconds);

/* ---------- text and errors (host_posix_win.c) */

/* UTF-16 of UTF-8 text, and back; malloc'd, NULL on failure (errno set) */
wchar_t *host_wide_from_utf8(const char *text);
char *host_utf8_from_wide(const wchar_t *text);
/* Linux's errno value (the guest's) for a Windows error code
(GetLastError) */
int host_linux_errno_of_windows(unsigned long error);
/* Linux's errno value for the C runtime's (host_common.c's host_errno,
the posix.h functions' errno) */
int host_linux_errno(int error);

/* ---------- the launcher (host_main_win.c, host_memory_win.c) */

/* the variable of the copy of the game the launcher starts, whose fixed
ranges it reserved before the copy ran: its value is the launcher's process
id */
#define HOST_LAUNCHED_VARIABLE L"HALO_WINDOWS_LAUNCHED"
/* whether this process is that copy; read once, at the first call (the top
of main), and the variable taken out of the environment, so that no
process this one starts (the self-updater's) takes itself for one */
int host_launched(void);

/* ---------- the process (host_main_win.c) */

/* the executable's path, UTF-8 with forward slashes (the guest's
/proc/self/exe); "" if unknown */
const char *host_executable_path(void);

/* ---------- the guest's system calls (host_syscall_win.c) */

/* the high-resolution sleep of the calling thread, in nanoseconds (the
guest's nanosleep, and the host's) */
void host_sleep_nanoseconds(uint64_t nanoseconds);
/* a sleep until a time of CLOCK_MONOTONIC (nanoseconds), on time: the
frames' limit's (host_windows_sleep_until) */
void host_sleep_until_nanoseconds(uint64_t deadline);

#endif
