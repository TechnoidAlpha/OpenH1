/*
HOST_CRASH_SIGNAL_TEST.C

The crash record a fatal signal writes (port/android/host/host_memory_watch.c's
handlers, host_common.c's record): in a child, each way the game can end on
a signal (abort(), the guest's raise() as the tgkill the host passes on,
host_abort's, a SIGABRT sent from outside, a segmentation fault), then the
record's file read back: its signal's line, its frames, the reason
host_abort gave, and the child ended by the signal itself. The control: a
child that exits writes none. Built and run by tools/test_host_units.py with
host_common.c's record (host_crash_record.inc); exits nonzero on a failure.
*/

#include "host.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			failures++; \
		} \
	} while (0)

/* ---------- the host functions the handlers call */

struct host_guest_image host_image;
#ifdef HALO_GUEST_BASE
uintptr_t host_guest_base;
#endif

void host_log(int priority, const char *text)
{
	(void)priority;
	(void)text;
}

void host_logf(int priority, const char *format, ...)
{
	(void)priority;
	(void)format;
}

size_t host_page_size(void)
{
	return (size_t)sysconf(_SC_PAGESIZE);
}

int host_memory_window_protection(uintptr_t address)
{
	(void)address;
	return PROT_READ | PROT_WRITE;
}

/* host_common.c's crash record and host_abort */
#include "host_crash_record.inc"

/* ---------- the children */

static char log_ring[] = "the guest's last line\n";
static uint64_t log_written = sizeof(log_ring) - 1;

static __attribute__((noinline)) void crash_by_abort(void)
{
	abort();
	__asm__ volatile("");
}

static __attribute__((noinline)) void crash_by_guest_raise(void)
{
#ifdef SYS_tgkill
	syscall(SYS_tgkill, getpid(), (pid_t)syscall(SYS_gettid), SIGABRT);
#else
	raise(SIGABRT);
#endif
	__asm__ volatile("");
}

static __attribute__((noinline)) void crash_by_host_abort(void)
{
	host_abort("the guest aborted");
	__asm__ volatile("");
}

static __attribute__((noinline)) void crash_by_fault(void)
{
	*(volatile int *)(uintptr_t)8 = 1;
	__asm__ volatile("");
}

enum
{
	CHILD_EXITS,
	CHILD_ABORT,
	CHILD_GUEST_RAISE,
	CHILD_HOST_ABORT,
	CHILD_SENT_ABORT,
	CHILD_FAULT,
};

static void child(int how, const char *path, int ready)
{
	host_crash_record(path, log_ring, (uint32_t)(sizeof(log_ring) - 1), &log_written);
	host_install_signal_handlers();
	switch (how)
	{
	case CHILD_EXITS:
		_exit(0);
	case CHILD_ABORT:
		crash_by_abort();
		break;
	case CHILD_GUEST_RAISE:
		crash_by_guest_raise();
		break;
	case CHILD_HOST_ABORT:
		crash_by_host_abort();
		break;
	case CHILD_SENT_ABORT:
		/* (the parent sends it once the handlers are in) */
		if (write(ready, "r", 1) != 1)
			_exit(3);
		for (;;)
			pause();
	case CHILD_FAULT:
		crash_by_fault();
		break;
	}
	_exit(2);
}

/* the record's file, read whole (empty if there is none) */
static void read_record(const char *path, char *text, size_t size)
{
	int file = open(path, O_RDONLY);
	size_t length = 0;
	ssize_t count;

	text[0] = 0;
	if (file < 0)
		return;
	while (length < size - 1 && (count = read(file, text + length, size - 1 - length)) > 0)
		length += (size_t)count;
	text[length] = 0;
	close(file);
}

static int count_of(const char *text, const char *part)
{
	int count = 0;

	for (; (text = strstr(text, part)) != NULL; text++)
		count++;
	return count;
}

static void test_child(const char *folder, int how, const char *name, int signal_number, const char *expected)
{
	char path[1024], text[65536], signal_line[64];
	int ready[2], status = 0;
	pid_t pid;

	snprintf(path, sizeof(path), "%s/crash-%s.txt", folder, name);
	unlink(path);
	CHECK(pipe(ready) == 0);
	pid = fork();
	if (pid == 0)
	{
		close(ready[0]);
		child(how, path, ready[1]);
	}
	close(ready[1]);
	if (how == CHILD_SENT_ABORT)
	{
		char byte;

		CHECK(read(ready[0], &byte, 1) == 1);
		kill(pid, SIGABRT);
	}
	close(ready[0]);
	CHECK(waitpid(pid, &status, 0) == pid);
	read_record(path, text, sizeof(text));
	if (!signal_number)
	{
		/* (the control: an exit writes none) */
		CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
		CHECK(access(path, F_OK) != 0);
		return;
	}
	/* (the signal's own end after the report: the default action) */
	CHECK(WIFSIGNALED(status) && WTERMSIG(status) == signal_number);
	snprintf(signal_line, sizeof(signal_line), "signal %d at address", signal_number);
	CHECK(count_of(text, signal_line) == 1);
	/* (the stack's frames, and the log after them) */
	CHECK(strstr(text, "frame  0: return") != NULL);
	CHECK(strstr(text, "--- the log's last lines\nthe guest's last line") != NULL);
	if (expected)
		CHECK(strstr(text, expected) != NULL);
	if (failures)
		printf("%s: the record:\n%s\n", name, text);
}

int main(int argc, char **argv)
{
	const char *folder = argc > 1 ? argv[1] : ".";

	test_child(folder, CHILD_EXITS, "exits", 0, NULL);
	test_child(folder, CHILD_ABORT, "abort", SIGABRT, "raised by the game itself");
	test_child(folder, CHILD_GUEST_RAISE, "guest-raise", SIGABRT, "raised by the game itself");
	test_child(folder, CHILD_HOST_ABORT, "host-abort", SIGABRT, "  the guest aborted\n");
	test_child(folder, CHILD_SENT_ABORT, "sent-abort", SIGABRT, "sent by process");
	test_child(folder, CHILD_FAULT, "fault", SIGSEGV, NULL);
	if (failures)
	{
		printf("%d failures\n", failures);
		return 1;
	}
	printf("host_crash_signal: all passed\n");
	return 0;
}
