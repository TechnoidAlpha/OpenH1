/*
HOST_MEMORY_WATCH_WIN.C

The 64-bit Windows host's exception handling: the write tracking's page
faults (port/android/host/host_memory_watch.c keeps the tracking itself,
with its locking, as on Linux; Linux's signal handlers are these), crash
reports, and the guest image's frames made walkable for Windows's
unwinder.

- The write tracking write-protects the pages behind the textures the
  renderer caches; a vectored exception handler, which Windows calls before
  any frame's handler, takes the first write to such a page
  (host_memory_watch_fault) and resumes the writer, in guest or host code.
- An exception no handler takes is a crash. On a thread that runs guest
  code, the stack it runs on has a first frame with a handler
  (host_stack_exception, host_enter_win.S) that reports it; the host's other
  threads have the unhandled exception filter. The report is the log's, as
  Linux's is: the exception, the registers and the frames.
- Windows walks a thread's frames with each function's unwinding data. The
  guest image, an ELF file loaded by hand, has none for Windows; it is built
  with frame pointers (tools/windows_build.py), so a function table
  callback over the image (RtlInstallFunctionTableCallback) describes every
  guest frame as a frame-pointer frame: push rbp; mov rbp, rsp. Windows's
  unwinder, which the exception dispatcher, the crash report and a debugger
  of the process use, then walks through guest frames to the host's.
*/

#include "host_win.h"

#include <windows.h>
#include <errno.h>

/* (tools/windows_build.py gives it, as to updater.c) */
#ifdef HALO_BUILD_NUMBER
#define HALO_CRASH_BUILD_NUMBER HALO_BUILD_NUMBER
#else
#define HALO_CRASH_BUILD_NUMBER 0
#endif
#include <stdio.h>
#include <string.h>

/* ---------- the write tracking's faults */

static LONG CALLBACK watch_handler(EXCEPTION_POINTERS *exception)
{
	EXCEPTION_RECORD *record = exception->ExceptionRecord;

	if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2 &&
		record->ExceptionInformation[0] == 1 /* a write */)
	{
		/* (the interrupted code's errors are kept: a fault can come between
		any call and its errno or GetLastError) */
		int saved_errno = errno;
		DWORD saved_error = GetLastError();
		int handled = host_memory_watch_fault((uintptr_t)record->ExceptionInformation[1]);

		SetLastError(saved_error);
		errno = saved_errno;
		if (handled)
			return EXCEPTION_CONTINUE_EXECUTION;
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

/* ---------- crash reports */

/* the module (DLL or executable) holding address, and the offset in it, as
text; or the guest image's offset */
static void describe_address(uint64_t address, char *text, size_t size)
{
	HMODULE module;
	wchar_t wide[MAX_PATH];
	char name[MAX_PATH * 3];
	const wchar_t *base;

	if (host_image.base && address >= host_image.base && address < host_image.end)
	{
		snprintf(text, size, "halo_guest.elf+0x%llx (addr2line -e halo_guest.elf 0x%llx)",
			(unsigned long long)(address - host_image.base), (unsigned long long)address);
		return;
	}
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCWSTR)(uintptr_t)address, &module) && GetModuleFileNameW(module, wide, MAX_PATH))
	{
		base = wcsrchr(wide, L'\\');
		WideCharToMultiByte(CP_UTF8, 0, base ? base + 1 : wide, -1, name, (int)sizeof(name), NULL, NULL);
		snprintf(text, size, "%s+0x%llx", name, (unsigned long long)(address - (uint64_t)(uintptr_t)module));
		return;
	}
	snprintf(text, size, "?");
}

/* a line of a crash's report: to halo.log, and to the crash record
(host_common.c) */
static void crash_line(const char *line)
{
	host_log(HOST_LOG_ERROR, line);
	host_crash_record_line(line);
}

/* whether a stack address lies on this thread's stack */
static int on_this_stack(uint64_t address)
{
	NT_TIB *tib = (NT_TIB *)NtCurrentTeb();

	return address >= (uint64_t)(uintptr_t)tib->StackLimit && address < (uint64_t)(uintptr_t)tib->StackBase;
}

void host_crash_report(void *exception_pointers)
{
	static volatile LONG crashed_thread = 0;
	EXCEPTION_POINTERS *pointers = exception_pointers;
	EXCEPTION_RECORD *record = pointers->ExceptionRecord;
	CONTEXT context = *pointers->ContextRecord;
	LONG thread = (LONG)GetCurrentThreadId();
	LONG first = InterlockedCompareExchange(&crashed_thread, thread, 0);
	char line[512], text[512], where[400];
	int frame;

	/* one report: a crash in this report ends it, and another thread's
	crash waits for the end the first one brings */
	if (first == thread)
		return;
	if (first)
		Sleep(INFINITE);
	/* the minidump first, before anything here can fail */
	if (host_crash_dump(pointers))
		host_log(HOST_LOG_ERROR, "crash: the crash reporter wrote a minidump (crashes\\ beside halo.exe)");
	/* (the crash record, the guest's reports\.crash.txt, begun with the
	exception: the next start offers to report it, its first line the
	crash's; halo.log is made afresh then) */
	describe_address(context.Rip, where, sizeof(where));
	snprintf(line, sizeof(line), "exception %08lx at %016llx (%s), thread %lu", (unsigned long)record->ExceptionCode,
		(unsigned long long)context.Rip, where, (unsigned long)GetCurrentThreadId());
	host_crash_record_begin(line);
	snprintf(text, sizeof(text), "crash: halo.exe at %p, the guest image at %08x, build %d", (void *)GetModuleHandleW(NULL),
		host_image.base, HALO_CRASH_BUILD_NUMBER);
	crash_line(text);
	host_log(HOST_LOG_ERROR, line);
	if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
	{
		snprintf(line, sizeof(line), "  %s of address %016llx",
			record->ExceptionInformation[0] == 0 ? "a read" : record->ExceptionInformation[0] == 1 ? "a write" :
			"execution", (unsigned long long)record->ExceptionInformation[1]);
		crash_line(line);
	}
	snprintf(line, sizeof(line), "  rax %016llx rbx %016llx rcx %016llx rdx %016llx", (unsigned long long)context.Rax,
		(unsigned long long)context.Rbx, (unsigned long long)context.Rcx, (unsigned long long)context.Rdx);
	crash_line(line);
	snprintf(line, sizeof(line), "  rsi %016llx rdi %016llx rbp %016llx rsp %016llx", (unsigned long long)context.Rsi,
		(unsigned long long)context.Rdi, (unsigned long long)context.Rbp, (unsigned long long)context.Rsp);
	crash_line(line);
	snprintf(line, sizeof(line), "  r8  %016llx r9  %016llx r10 %016llx r11 %016llx", (unsigned long long)context.R8,
		(unsigned long long)context.R9, (unsigned long long)context.R10, (unsigned long long)context.R11);
	crash_line(line);
	snprintf(line, sizeof(line), "  r12 %016llx r13 %016llx r14 %016llx r15 %016llx", (unsigned long long)context.R12,
		(unsigned long long)context.R13, (unsigned long long)context.R14, (unsigned long long)context.R15);
	crash_line(line);
	/* the frames, by Windows's unwinder (the guest's through the function
	table); each step only while the stack pointer stays on this stack */
	for (frame = 0; frame < 48; frame++)
	{
		DWORD64 image_base, establisher;
		PVOID handler_data;
		PRUNTIME_FUNCTION function;
		uint64_t previous = context.Rsp;

		describe_address(context.Rip, where, sizeof(where));
		snprintf(line, sizeof(line), "  frame %2d: %016llx %s", frame, (unsigned long long)context.Rip, where);
		crash_line(line);
		/* (a guest frame is read through rbp: it must be on the stack) */
		if (host_image.base && context.Rip >= host_image.base && context.Rip < host_image.end &&
			!on_this_stack(context.Rbp))
		{
			break;
		}
		function = RtlLookupFunctionEntry(context.Rip, &image_base, NULL);
		if (function)
		{
			RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context, &handler_data,
				&establisher, NULL);
		}
		else
		{
			/* (a leaf function: the return address on the stack) */
			if (!on_this_stack(context.Rsp))
				break;
			context.Rip = *(DWORD64 *)(uintptr_t)context.Rsp;
			context.Rsp += 8;
		}
		if (!context.Rip || !on_this_stack(context.Rsp) || context.Rsp < previous)
			break;
	}
	/* (the guest's log after the crash's lines: report.c's) */
	host_crash_record_end();
}

int host_stack_exception(void *record_pointer, void *frame, void *context, void *dispatcher)
{
	EXCEPTION_RECORD *record = record_pointer;
	EXCEPTION_POINTERS pointers;

	(void)frame;
	(void)dispatcher;
	if (record->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
		return ExceptionContinueSearch;
	/* (an informational exception raised by software, which no one took:
	a debugger's thread name or output, with no debugger) */
	if ((record->ExceptionCode >> 30) == 1 && !(record->ExceptionFlags & EXCEPTION_NONCONTINUABLE))
		return ExceptionContinueExecution;
	if (record->ExceptionCode == EXCEPTION_STACK_OVERFLOW)
	{
		/* (little stack is left: no walk) */
		host_log(HOST_LOG_FATAL, "stack overflow");
		if (host_crash_record_begin("stack overflow"))
			host_crash_record_end();
		TerminateProcess(GetCurrentProcess(), EXCEPTION_STACK_OVERFLOW);
	}
	pointers.ExceptionRecord = record;
	pointers.ContextRecord = context;
	host_crash_report(&pointers);
	host_log(HOST_LOG_FATAL, "the game crashed");
	TerminateProcess(GetCurrentProcess(), record->ExceptionCode);
	return ExceptionContinueSearch;
}

/* the host's own threads (the render thread, SDL's) */
static LONG WINAPI unhandled_filter(EXCEPTION_POINTERS *pointers)
{
	host_crash_report(pointers);
	host_log(HOST_LOG_FATAL, "the game crashed");
	TerminateProcess(GetCurrentProcess(), pointers->ExceptionRecord->ExceptionCode);
	return EXCEPTION_EXECUTE_HANDLER;
}

void host_install_signal_handlers(void)
{
	AddVectoredExceptionHandler(1, watch_handler);
	SetUnhandledExceptionFilter(unhandled_filter);
}

/* ---------- the guest's frames for Windows's unwinder

Every guest function starts push rbp; mov rbp, rsp (the image is built with
frame pointers), so one description fits every frame past its first four
bytes: the frame register is rbp, and below it the caller's rbp, then the
return address. The table's entry for an address starts 16 bytes before it,
so that the unwinder takes the frame as set up; the epilogue it recognises
in the code itself (pop rbp; ret). A function's first instructions, and
code without a frame (the import stubs' jump), are misdescribed: the walk
from there may skip a frame. */

/* UNWIND_INFO: version 1, no handler; a prologue of 4 bytes; two codes;
the frame register rbp (5) at offset 0. The codes, last instruction first:
at offset 4, UWOP_SET_FPREG (3); at offset 1, UWOP_PUSH_NONVOL (0) of rbp. */
static const unsigned char frame_pointer_unwind[8] = { 0x01, 4, 2, 0x05, 4, 0x03, 1, 0x50 };

static uint32_t unwind_offset;

static PRUNTIME_FUNCTION CALLBACK guest_function_entry(DWORD64 pc, PVOID context)
{
	static _Thread_local RUNTIME_FUNCTION entry;
	uint32_t offset = (uint32_t)(pc - host_image.base);

	(void)context;
	entry.BeginAddress = offset >= 16 ? offset - 16 : 0;
	entry.EndAddress = offset + 1;
	entry.UnwindData = unwind_offset;
	return &entry;
}

void host_register_guest_frames(void)
{
	/* (the description must lie at a 32-bit offset from the image's base:
	in its range, past it) */
	unsigned char *unwind = host_memory_image_extra(sizeof(frame_pointer_unwind));

	if (!unwind)
	{
		host_logf(HOST_LOG_WARN, "no room for the guest's unwinding data: crash reports stop at guest frames");
		return;
	}
	memcpy(unwind, frame_pointer_unwind, sizeof(frame_pointer_unwind));
	unwind_offset = (uint32_t)((uintptr_t)unwind - host_image.base);
	if (!RtlInstallFunctionTableCallback((DWORD64)host_image.base | 3, host_image.base,
		host_image.end - host_image.base, guest_function_entry, NULL, NULL))
	{
		host_logf(HOST_LOG_WARN, "cannot install the guest's function table: crash reports stop at guest frames");
	}
}
