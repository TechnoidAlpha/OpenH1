/* A write that makes no progress fails (port/linux/src/xbox_files.c's
write_at): a write() or pwrite() that writes nothing of a request that is
not empty, without an error, ends the write as an error with the bytes
written before it, where it was tried again for good. Interrupted writes are
tried again, short ones go on from where they stopped, an error is the
error, and an empty request writes nothing. The system's write and pwrite
are made-up ones that play a script of results; each write is counted, and
a run that keeps writing past a bound is a write that never ends. */
#include "harness.h"
#include <errno.h>
#include <stdarg.h>
#include <sys/types.h>

typedef int BOOL;
typedef unsigned long DWORD;
typedef DWORD *LPDWORD;
typedef const void *LPCVOID;

struct platform_file { int descriptor; char path[1024]; };

/* the script: each write's result (bytes, 0, or -errno), the last kept */
static long script[8];
static int script_count, writes, last_error;
static unsigned long long last_offset;
static char disk[64];

static ssize_t played(const void *buffer, size_t count, unsigned long long offset)
{
	long result = script[writes < script_count ? writes : script_count - 1];

	CHECK(++writes < 1000, "a write that makes no progress tried again for good");
	if (result < 0)
	{
		errno = (int)-result;
		return -1;
	}
	if ((size_t)result > count)
		result = (long)count;
	memcpy(disk + offset, buffer, (size_t)result);
	last_offset = offset;
	return result;
}
static unsigned long long position;
static ssize_t fake_write(int descriptor, const void *buffer, size_t count)
{
	ssize_t result = played(buffer, count, position);

	(void)descriptor;
	if (result > 0)
		position += (unsigned long long)result;
	return result;
}
static ssize_t fake_pwrite(int descriptor, const void *buffer, size_t count, off_t offset)
{
	(void)descriptor;
	return played(buffer, count, (unsigned long long)offset);
}
#define write fake_write
#define pwrite fake_pwrite

static int config_boolean(const char *name) { (void)name; return 0; }
static int halo_load_trace_enabled(void) { return 0; }
static double load_io_now(void) { return 0.0; }
static void load_trace_log(const char *format, ...) { (void)format; }
static void platform_set_last_error_from_errno(int error) { last_error = error; }

#include "under_test.inc"

static struct platform_file file;

/* a write of count bytes of "abcdef..." through the script given */
static BOOL written(int positioned, DWORD count, DWORD *done, int results, ...)
{
	static const char text[] = "abcdefghijklmnopqrstuvwxyz";
	va_list list;
	int index;

	va_start(list, results);
	for (index = 0; index < results; index++)
		script[index] = va_arg(list, long);
	va_end(list);
	script_count = results;
	writes = 0;
	last_error = 0;
	position = 0;
	memset(disk, 0, sizeof(disk));
	*done = 12345;
	return write_at(&file, text, count, done, positioned, positioned ? 8 : 0);
}

int main(int argc, char **argv)
{
	const char *case_name = argc > 1 ? argv[1] : "";
	DWORD done;

	CASE("zero_at_once")
	{
		CHECK(!written(0, 6, &done, 1, 0L), "a write of nothing succeeded");
		CHECK(done == 0, "%lu bytes said written, not 0", done);
		CHECK(last_error == EIO, "the error %d, not EIO", last_error);
	}
	else CASE("partial_then_zero")
	{
		CHECK(!written(1, 6, &done, 2, 2L, 0L), "a write that stopped succeeded");
		CHECK(done == 2, "%lu bytes said written, not 2", done);
		CHECK(memcmp(disk + 8, "ab", 2) == 0, "the first bytes not at the offset");
	}
	else CASE("interrupted")
	{
		CHECK(written(0, 6, &done, 2, (long)-EINTR, 6L), "an interrupted write failed");
		CHECK(done == 6 && writes == 2, "%lu bytes in %d writes", done, writes);
	}
	else CASE("short_writes")
	{
		CHECK(written(1, 6, &done, 3, 2L, 3L, 1L), "short writes failed");
		CHECK(done == 6 && writes == 3, "%lu bytes in %d writes", done, writes);
		CHECK(memcmp(disk + 8, "abcdef", 6) == 0 && last_offset == 13, "the short writes not each at its place");
	}
	else CASE("error")
	{
		CHECK(!written(0, 6, &done, 2, 3L, (long)-ENOSPC), "a failed write succeeded");
		CHECK(done == 3 && last_error == ENOSPC, "%lu bytes, error %d", done, last_error);
	}
	else CASE("empty")
	{
		CHECK(written(0, 0, &done, 1, 0L), "an empty write failed");
		CHECK(done == 0 && writes == 0, "%lu bytes in %d writes", done, writes);
	}
	else
	{
		fprintf(stderr, "unknown case %s\n", case_name);
		return 2;
	}
	return 0;
}
