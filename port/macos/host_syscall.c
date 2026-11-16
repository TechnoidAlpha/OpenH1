/*
HOST_SYSCALL.C

The guest's system calls on macOS (port/android/host/host_syscall.c is the
Linux and Android hosts'). The guest's musl was built for Linux
(port/android/guest/libc/arch/arm64_32): its system calls have Linux's
numbers, flags, structure layouts and error numbers, which this file
translates into the Darwin calls that do the same, and back.

Pointers are guest addresses (host.h). Calls the game's runtime never makes
fail with ENOSYS, logged once.
*/

#include "host.h"
#include "halo_guest_syscall.h"

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <os/os_sync_wait_on_address.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

/* the guest's system call numbers, __NR_* */
#include "bits/syscall.h.in"

/* ---------- Linux's values */

#define LINUX_AT_FDCWD -100
#define LINUX_AT_SYMLINK_NOFOLLOW 0x100
#define LINUX_AT_REMOVEDIR 0x200
#define LINUX_AT_EACCESS 0x200
#define LINUX_AT_EMPTY_PATH 0x1000

#define LINUX_O_ACCMODE 03
#define LINUX_O_CREAT 0100
#define LINUX_O_EXCL 0200
#define LINUX_O_NOCTTY 0400
#define LINUX_O_TRUNC 01000
#define LINUX_O_APPEND 02000
#define LINUX_O_NONBLOCK 04000
#define LINUX_O_DSYNC 010000
#define LINUX_O_SYNC 04010000
#define LINUX_O_DIRECTORY 040000
#define LINUX_O_NOFOLLOW 0100000
#define LINUX_O_CLOEXEC 02000000
#define LINUX_O_TMPFILE 020000000

#define LINUX_F_DUPFD_CLOEXEC 1030

#define LINUX_CLOCK_REALTIME 0
#define LINUX_CLOCK_MONOTONIC 1
#define LINUX_CLOCK_PROCESS_CPUTIME_ID 2
#define LINUX_CLOCK_THREAD_CPUTIME_ID 3
#define LINUX_CLOCK_MONOTONIC_RAW 4
#define LINUX_CLOCK_REALTIME_COARSE 5
#define LINUX_CLOCK_MONOTONIC_COARSE 6
#define LINUX_CLOCK_BOOTTIME 7
#define LINUX_TIMER_ABSTIME 1

#define LINUX_FUTEX_WAIT 0
#define LINUX_FUTEX_WAKE 1
#define LINUX_FUTEX_REQUEUE 3
#define LINUX_FUTEX_CMP_REQUEUE 4
#define LINUX_FUTEX_WAIT_BITSET 9
#define LINUX_FUTEX_WAKE_BITSET 10
#define LINUX_FUTEX_CMD_MASK 0x7f

#define LINUX_MADV_DONTNEED 4

#define LINUX_SIGABRT 6
#define LINUX_RLIMIT_STACK 3
#define LINUX_RLIMIT_NOFILE 7
#define LINUX_UTIME_NOW ((1 << 30) - 1)
#define LINUX_UTIME_OMIT ((1 << 30) - 2)

/* the guest's structures */
struct guest_timespec
{
	int32_t seconds;
	int32_t nanoseconds;
};

struct guest_iovec
{
	uint32_t base;
	uint32_t length;
};

/* Linux's struct stat on AArch64 (the guest's kstat.h) */
struct linux_stat
{
	uint64_t dev;
	uint64_t ino;
	uint32_t mode;
	uint32_t nlink;
	uint32_t uid;
	uint32_t gid;
	uint64_t rdev;
	uint64_t pad;
	int64_t size;
	int32_t blksize;
	int32_t pad2;
	int64_t blocks;
	int64_t atime_sec, atime_nsec;
	int64_t mtime_sec, mtime_nsec;
	int64_t ctime_sec, ctime_nsec;
	uint32_t unused[2];
};

struct linux_rlimit
{
	uint64_t current;
	uint64_t maximum;
};

#define GUEST(type, value) ((type)host_pointer((uint32_t)(value)))

/* ---------- error numbers */

/* Linux's number for a Darwin errno */
int host_linux_errno(int error)
{
	switch (error)
	{
	case 0: return 0;
	case EDEADLK: return 35;
	case EAGAIN: return 11;
	case EINPROGRESS: return 115;
	case EALREADY: return 114;
	case ENOTSOCK: return 88;
	case EDESTADDRREQ: return 89;
	case EMSGSIZE: return 90;
	case EPROTOTYPE: return 91;
	case ENOPROTOOPT: return 92;
	case EPROTONOSUPPORT: return 93;
	case ESOCKTNOSUPPORT: return 94;
	case ENOTSUP: return 95;
	case EOPNOTSUPP: return 95;
	case EPFNOSUPPORT: return 96;
	case EAFNOSUPPORT: return 97;
	case EADDRINUSE: return 98;
	case EADDRNOTAVAIL: return 99;
	case ENETDOWN: return 100;
	case ENETUNREACH: return 101;
	case ENETRESET: return 102;
	case ECONNABORTED: return 103;
	case ECONNRESET: return 104;
	case ENOBUFS: return 105;
	case EISCONN: return 106;
	case ENOTCONN: return 107;
	case ESHUTDOWN: return 108;
	case ETOOMANYREFS: return 109;
	case ETIMEDOUT: return 110;
	case ECONNREFUSED: return 111;
	case ELOOP: return 40;
	case ENAMETOOLONG: return 36;
	case EHOSTDOWN: return 112;
	case EHOSTUNREACH: return 113;
	case ENOTEMPTY: return 39;
	case EUSERS: return 87;
	case EDQUOT: return 122;
	case ESTALE: return 116;
	case ENOLCK: return 37;
	case ENOSYS: return 38;
	case EOVERFLOW: return 75;
	case ECANCELED: return 125;
	case EIDRM: return 43;
	case ENOMSG: return 42;
	case EILSEQ: return 84;
	case EBADMSG: return 74;
	case EMULTIHOP: return 72;
	case ENOLINK: return 67;
	case EPROTO: return 71;
	case ETIME: return 62;
	case ENOTRECOVERABLE: return 131;
	case EOWNERDEAD: return 130;
	default:
		/* EPERM (1) to ERANGE (34) are the same, but for EDEADLK */
		return error >= 1 && error <= 34 ? error : 5;
	}
}

static long result_of(long value)
{
	return value == -1 ? -host_linux_errno(errno) : value;
}

/* ---------- flags */

static int darwin_directory(long long descriptor)
{
	return (int)descriptor == LINUX_AT_FDCWD ? AT_FDCWD : (int)descriptor;
}

static int darwin_open_flags(int flags)
{
	int result = flags & LINUX_O_ACCMODE;

	result |= flags & LINUX_O_CREAT ? O_CREAT : 0;
	result |= flags & LINUX_O_EXCL ? O_EXCL : 0;
	result |= flags & LINUX_O_NOCTTY ? O_NOCTTY : 0;
	result |= flags & LINUX_O_TRUNC ? O_TRUNC : 0;
	result |= flags & LINUX_O_APPEND ? O_APPEND : 0;
	result |= flags & LINUX_O_NONBLOCK ? O_NONBLOCK : 0;
	result |= (flags & LINUX_O_SYNC) == LINUX_O_SYNC ? O_SYNC : flags & LINUX_O_DSYNC ? O_DSYNC : 0;
	result |= flags & LINUX_O_DIRECTORY ? O_DIRECTORY : 0;
	result |= flags & LINUX_O_NOFOLLOW ? O_NOFOLLOW : 0;
	result |= flags & LINUX_O_CLOEXEC ? O_CLOEXEC : 0;
	/* (O_LARGEFILE, O_NOATIME, O_DIRECT and O_PATH go: a descriptor opened
	for reading serves O_PATH's uses) */
	return result;
}

static int linux_open_flags(int flags)
{
	int result = flags & O_ACCMODE;

	result |= flags & O_APPEND ? LINUX_O_APPEND : 0;
	result |= flags & O_NONBLOCK ? LINUX_O_NONBLOCK : 0;
	return result;
}

static int darwin_at_flags(long long flags)
{
	return flags & LINUX_AT_SYMLINK_NOFOLLOW ? AT_SYMLINK_NOFOLLOW : 0;
}

/* ---------- time */

static int timespec_in(uint64_t address, struct timespec *result)
{
	const struct guest_timespec *value = GUEST(const struct guest_timespec *, address);

	if (!value)
		return 0;
	result->tv_sec = value->seconds;
	result->tv_nsec = value->nanoseconds;
	return 1;
}

static void timespec_out(uint64_t address, const struct timespec *value)
{
	struct guest_timespec *result = GUEST(struct guest_timespec *, address);

	if (!result)
		return;
	result->seconds = (int32_t)value->tv_sec;
	result->nanoseconds = (int32_t)value->tv_nsec;
}

static int darwin_clock(long long clock, clockid_t *result)
{
	switch (clock)
	{
	case LINUX_CLOCK_REALTIME:
	case LINUX_CLOCK_REALTIME_COARSE:
		*result = CLOCK_REALTIME;
		return 1;
	case LINUX_CLOCK_MONOTONIC:
	case LINUX_CLOCK_MONOTONIC_COARSE:
	case LINUX_CLOCK_BOOTTIME:
		*result = CLOCK_MONOTONIC;
		return 1;
	case LINUX_CLOCK_MONOTONIC_RAW:
		*result = CLOCK_MONOTONIC_RAW;
		return 1;
	case LINUX_CLOCK_PROCESS_CPUTIME_ID:
		*result = CLOCK_PROCESS_CPUTIME_ID;
		return 1;
	case LINUX_CLOCK_THREAD_CPUTIME_ID:
		*result = CLOCK_THREAD_CPUTIME_ID;
		return 1;
	default:
		return 0;
	}
}

static uint64_t nanoseconds(const struct timespec *value)
{
	return (uint64_t)value->tv_sec * 1000000000ULL + (uint64_t)value->tv_nsec;
}

/* ---------- futexes, on os_sync_wait_on_address */

static long futex_wake(void *address, long long count)
{
	long woken = 0;

	if (count <= 0)
		return 0;
	if (count >= 0x7fffffff)
	{
		/* (all of them; how many is not said) */
		os_sync_wake_by_address_all(address, 4, OS_SYNC_WAKE_BY_ADDRESS_NONE);
		return 1;
	}
	while (woken < count && os_sync_wake_by_address_any(address, 4, OS_SYNC_WAKE_BY_ADDRESS_NONE) == 0)
		woken++;
	return woken;
}

static long futex(uint64_t guest, int operation, uint32_t value, uint64_t timeout, uint64_t guest2, uint32_t value3)
{
	uint32_t *address = GUEST(uint32_t *, guest);
	int command = operation & LINUX_FUTEX_CMD_MASK;

	switch (command)
	{
	case LINUX_FUTEX_WAIT:
	case LINUX_FUTEX_WAIT_BITSET:
	{
		struct timespec limit;
		int result;

		if (timespec_in(timeout, &limit))
		{
			uint64_t wait = nanoseconds(&limit);

			if (command == LINUX_FUTEX_WAIT_BITSET)
			{
				/* (an absolute time, on the clock the flag names) */
				struct timespec now;

				clock_gettime(operation & 256 ? CLOCK_REALTIME : CLOCK_MONOTONIC, &now);
				wait = wait > nanoseconds(&now) ? wait - nanoseconds(&now) : 0;
			}
			if (!wait)
				return *(volatile uint32_t *)address == value ? -110 : -11;
			result = os_sync_wait_on_address_with_timeout(address, value, 4, OS_SYNC_WAIT_ON_ADDRESS_NONE,
				OS_CLOCK_MACH_ABSOLUTE_TIME, wait);
		}
		else
		{
			result = os_sync_wait_on_address(address, value, 4, OS_SYNC_WAIT_ON_ADDRESS_NONE);
		}
		if (result < 0)
			return errno == ETIMEDOUT ? -110 : errno == EINTR ? -4 : -host_linux_errno(errno);
		return 0;
	}
	case LINUX_FUTEX_WAKE:
	case LINUX_FUTEX_WAKE_BITSET:
		return futex_wake(address, value);
	case LINUX_FUTEX_CMP_REQUEUE:
		if (*(volatile uint32_t *)address != value3)
			return -11;
		/* fall through */
	case LINUX_FUTEX_REQUEUE:
		/* every waiter on the first address is woken: those the call would
		wake, and those it would move to the second address, which wait
		there again themselves (a spurious wakeup, which every futex user
		allows). musl's condition variables hand a broadcast on this way
		(unlock_requeue: wake 0, move 1), so waking only `value` would leave
		the moved waiters asleep for good */
		(void)guest2;
		return futex_wake(address, 0x7fffffff);
	default:
		return -38;
	}
}

/* ---------- directories: getdents64 over a DIR of the descriptor */

#define DIRECTORY_STREAMS 64

static struct
{
	int descriptor;
	DIR *stream;
	/* an entry that did not fit last time */
	struct dirent pending;
	int has_pending;
} streams[DIRECTORY_STREAMS];
static pthread_mutex_t streams_lock = PTHREAD_MUTEX_INITIALIZER;

static void stream_forget(int descriptor)
{
	int index;

	pthread_mutex_lock(&streams_lock);
	for (index = 0; index < DIRECTORY_STREAMS; index++)
	{
		if (streams[index].stream && streams[index].descriptor == descriptor)
		{
			closedir(streams[index].stream);
			streams[index].stream = NULL;
		}
	}
	pthread_mutex_unlock(&streams_lock);
}

static long getdents64(int descriptor, unsigned char *buffer, uint32_t size)
{
	int index, slot = -1;
	uint32_t used = 0;

	pthread_mutex_lock(&streams_lock);
	for (index = 0; index < DIRECTORY_STREAMS && slot < 0; index++)
	{
		if (streams[index].stream && streams[index].descriptor == descriptor)
			slot = index;
	}
	if (slot < 0)
	{
		int copy = dup(descriptor);
		DIR *stream = copy >= 0 ? fdopendir(copy) : NULL;

		for (index = 0; index < DIRECTORY_STREAMS && slot < 0; index++)
		{
			if (!streams[index].stream)
				slot = index;
		}
		if (!stream || slot < 0)
		{
			int error = stream ? EMFILE : errno;

			if (stream)
				closedir(stream);
			else if (copy >= 0)
				close(copy);
			pthread_mutex_unlock(&streams_lock);
			return -host_linux_errno(error);
		}
		streams[slot].descriptor = descriptor;
		streams[slot].stream = stream;
		streams[slot].has_pending = 0;
	}
	for (;;)
	{
		struct dirent entry, *next;
		size_t length;
		uint32_t record;

		if (streams[slot].has_pending)
		{
			entry = streams[slot].pending;
			streams[slot].has_pending = 0;
		}
		else
		{
			next = readdir(streams[slot].stream);
			if (!next)
				break;
			entry = *next;
		}
		/* struct linux_dirent64: ino, off (8 bytes each), reclen (2),
		type (1), the name, 8-byte aligned */
		length = strlen(entry.d_name);
		record = (uint32_t)((19 + length + 1 + 7) & ~7u);
		if (used + record > size)
		{
			streams[slot].pending = entry;
			streams[slot].has_pending = 1;
			break;
		}
		memset(buffer + used, 0, record);
		memcpy(buffer + used, &entry.d_ino, 8);
		*(int64_t *)(void *)(buffer + used + 8) = (int64_t)(used + record);
		*(uint16_t *)(void *)(buffer + used + 16) = (uint16_t)record;
		buffer[used + 18] = entry.d_type;
		memcpy(buffer + used + 19, entry.d_name, length);
		used += record;
	}
	pthread_mutex_unlock(&streams_lock);
	if (!used && streams[slot].has_pending)
		return -22;
	return used;
}

/* ---------- files */

static long stat_out(const struct stat *value, uint64_t address)
{
	struct linux_stat *result = GUEST(struct linux_stat *, address);

	if (!result)
		return -14;
	memset(result, 0, sizeof(*result));
	result->dev = (uint64_t)value->st_dev;
	result->ino = value->st_ino;
	result->mode = value->st_mode;
	result->nlink = value->st_nlink;
	result->uid = value->st_uid;
	result->gid = value->st_gid;
	result->rdev = (uint64_t)value->st_rdev;
	result->size = value->st_size;
	result->blksize = value->st_blksize;
	result->blocks = value->st_blocks;
	result->atime_sec = value->st_atimespec.tv_sec;
	result->atime_nsec = value->st_atimespec.tv_nsec;
	result->mtime_sec = value->st_mtimespec.tv_sec;
	result->mtime_nsec = value->st_mtimespec.tv_nsec;
	result->ctime_sec = value->st_ctimespec.tv_sec;
	result->ctime_nsec = value->st_ctimespec.tv_nsec;
	return 0;
}

#define NOT_PROC -1000

/* /proc/self/exe and /proc/self/fd/N, which musl and the platform layer
read; else NOT_PROC */
static long proc_link(const char *path, char *buffer, size_t size)
{
	char target[PATH_MAX];
	size_t length;
	int descriptor;

	if (!strcmp(path, "/proc/self/exe"))
	{
		uint32_t capacity = sizeof(target);
		extern int _NSGetExecutablePath(char *, uint32_t *);

		if (_NSGetExecutablePath(target, &capacity) != 0)
			return -36;
	}
	else if (sscanf(path, "/proc/self/fd/%d", &descriptor) == 1)
	{
		if (fcntl(descriptor, F_GETPATH, target) == -1)
			return -host_linux_errno(errno);
	}
	else
	{
		return NOT_PROC;
	}
	length = strlen(target);
	if (length > size)
		length = size;
	memcpy(buffer, target, length);
	return (long)length;
}

static long guest_fcntl(int descriptor, long long command, long long argument)
{
	switch (command)
	{
	case F_DUPFD:
		return result_of(fcntl(descriptor, F_DUPFD, (int)argument));
	case LINUX_F_DUPFD_CLOEXEC:
		return result_of(fcntl(descriptor, F_DUPFD_CLOEXEC, (int)argument));
	case F_GETFD:
		return result_of(fcntl(descriptor, F_GETFD));
	case F_SETFD:
		return result_of(fcntl(descriptor, F_SETFD, (int)argument & FD_CLOEXEC));
	case F_GETFL:
	{
		int flags = fcntl(descriptor, F_GETFL);

		return flags == -1 ? result_of(-1) : linux_open_flags(flags);
	}
	case F_SETFL:
		return result_of(fcntl(descriptor, F_SETFL, darwin_open_flags((int)argument) & (O_APPEND | O_NONBLOCK)));
	default:
		return -22;
	}
}

/* ---------- reading and writing */

static long guest_vector(int descriptor, uint64_t vector, int count, int64_t offset, int positional, int write)
{
	struct iovec host_vector[64];
	const struct guest_iovec *guest = GUEST(const struct guest_iovec *, vector);
	int index;

	if (count < 0 || count > 64)
		return -22;
	for (index = 0; index < count; index++)
	{
		host_vector[index].iov_base = GUEST(void *, guest[index].base);
		host_vector[index].iov_len = guest[index].length;
	}
	if (write)
		return result_of(positional ? pwritev(descriptor, host_vector, count, offset) :
			writev(descriptor, host_vector, count));
	return result_of(positional ? preadv(descriptor, host_vector, count, offset) : readv(descriptor, host_vector, count));
}

/* ---------- the computer's memory */

/* sysinfo's memory (the game's texture cache is sized by it,
halo_port_capacity.h) and uptime: the memory is hw.memsize, the free part
the free pages; counted in units as large as their 32 bits need, as a 64-bit
Linux kernel does for its 32-bit programs */
static long guest_sysinfo(struct halo_guest_sysinfo *result)
{
	uint64_t total = 0, unit = 1;
	uint32_t free_pages = 0;
	size_t size = sizeof(total);
	struct timespec now;

	if (sysctlbyname("hw.memsize", &total, &size, NULL, 0) != 0)
		return result_of(-1);
	size = sizeof(free_pages);
	if (sysctlbyname("vm.page_free_count", &free_pages, &size, NULL, 0) != 0)
		free_pages = 0;
	while (total / unit > UINT32_MAX)
		unit <<= 1;
	clock_gettime(CLOCK_MONOTONIC, &now);
	memset(result, 0, sizeof(*result));
	result->uptime = (uint32_t)now.tv_sec;
	result->totalram = (uint32_t)(total / unit);
	result->freeram = (uint32_t)((uint64_t)free_pages * (uint64_t)getpagesize() / unit);
	result->mem_unit = (uint32_t)unit;
	return 0;
}

/* ---------- dispatch */

static long long dispatch(long long number, long long a, long long b, long long c, long long d, long long e,
	long long f);

/* HALO_TRACE_SYSCALLS=1 logs each call and its result */
long long host_syscall(long long number, long long a, long long b, long long c,
	long long d, long long e, long long f)
{
	static int trace = -1;
	long long result;

	if (trace < 0)
		trace = getenv("HALO_TRACE_SYSCALLS") && *getenv("HALO_TRACE_SYSCALLS") == '1';
	result = dispatch(number, a, b, c, d, e, f);
	if (trace)
		host_logf(HOST_LOG_INFO, "syscall %lld (%llx, %llx, %llx, %llx, %llx, %llx) = %lld", number,
			(unsigned long long)a, (unsigned long long)b, (unsigned long long)c, (unsigned long long)d,
			(unsigned long long)e, (unsigned long long)f, result);
	return result;
}

static long long dispatch(long long number, long long a, long long b, long long c, long long d, long long e,
	long long f)
{
	switch (number)
	{
	case __NR_read:
		return result_of(read((int)a, GUEST(void *, b), (size_t)(uint32_t)c));
	case __NR_write:
		return result_of(write((int)a, GUEST(const void *, b), (size_t)(uint32_t)c));
	case __NR_pread64:
		return result_of(pread((int)a, GUEST(void *, b), (size_t)(uint32_t)c, (off_t)d));
	case __NR_pwrite64:
		return result_of(pwrite((int)a, GUEST(const void *, b), (size_t)(uint32_t)c, (off_t)d));
	case __NR_readv:
		return guest_vector((int)a, (uint64_t)b, (int)c, 0, 0, 0);
	case __NR_writev:
		return guest_vector((int)a, (uint64_t)b, (int)c, 0, 0, 1);
	case __NR_preadv:
		return guest_vector((int)a, (uint64_t)b, (int)c, d, 1, 0);
	case __NR_pwritev:
		return guest_vector((int)a, (uint64_t)b, (int)c, d, 1, 1);
	case __NR__llseek:
	{
		/* (the offset in two halves, and the result through a pointer: the
		guest's system calls return 32 bits, halo_guest_syscall.h) */
		int64_t offset = (int64_t)(((uint64_t)(uint32_t)b << 32) | (uint32_t)c);
		off_t position;

		if (!(uint32_t)d)
			return -14; /* EFAULT */
		position = lseek((int)a, (off_t)offset, (int)e);
		if (position == (off_t)-1)
			return result_of(-1);
		memcpy(GUEST(void *, d), &(int64_t){ (int64_t)position }, sizeof(int64_t));
		return 0;
	}
	case __NR_openat:
		if ((int)c & LINUX_O_TMPFILE)
			return -95;
		return result_of(openat(darwin_directory(a), GUEST(const char *, b), darwin_open_flags((int)c), (mode_t)d));
	case __NR_close:
		stream_forget((int)a);
		return result_of(close((int)a));
	case __NR_getdents64:
		return getdents64((int)a, GUEST(unsigned char *, b), (uint32_t)c);
	case __NR_fstat:
	{
		struct stat value;

		if (fstat((int)a, &value) == -1)
			return result_of(-1);
		return stat_out(&value, (uint64_t)b);
	}
	case __NR_newfstatat:
	{
		struct stat value;
		const char *path = GUEST(const char *, b);

		if ((d & LINUX_AT_EMPTY_PATH) && (!path || !path[0]))
		{
			if (fstat((int)a, &value) == -1)
				return result_of(-1);
		}
		else if (fstatat(darwin_directory(a), path, &value, darwin_at_flags(d)) == -1)
		{
			return result_of(-1);
		}
		return stat_out(&value, (uint64_t)c);
	}
	case __NR_unlinkat:
		return result_of(unlinkat(darwin_directory(a), GUEST(const char *, b), c & LINUX_AT_REMOVEDIR ? AT_REMOVEDIR : 0));
	case __NR_renameat2:
		if (e)
			return -22;
		/* fall through */
	case __NR_renameat:
		return result_of(renameat(darwin_directory(a), GUEST(const char *, b), darwin_directory(c), GUEST(const char *, d)));
	case __NR_mkdirat:
		return result_of(mkdirat(darwin_directory(a), GUEST(const char *, b), (mode_t)c));
	case __NR_fchmod:
		return result_of(fchmod((int)a, (mode_t)b));
	case __NR_fchmodat:
		return result_of(fchmodat(darwin_directory(a), GUEST(const char *, b), (mode_t)c, darwin_at_flags(d)));
	case __NR_faccessat:
	case __NR_faccessat2:
		return result_of(faccessat(darwin_directory(a), GUEST(const char *, b), (int)c,
			(number == __NR_faccessat2 && (d & LINUX_AT_EACCESS)) ? AT_EACCESS : 0));
	case __NR_ftruncate:
		return result_of(ftruncate((int)a, (off_t)b));
	case __NR_fsync:
	case __NR_fdatasync:
		return result_of(fsync((int)a));
	case __NR_fcntl:
		return guest_fcntl((int)a, b, c);
	case __NR_flock:
		return result_of(flock((int)a, (int)b));
	case __NR_getcwd:
	{
		char *buffer = GUEST(char *, a);

		if (!getcwd(buffer, (size_t)(uint32_t)b))
			return result_of(-1);
		return (long)strlen(buffer) + 1;
	}
	case __NR_chdir:
		return result_of(chdir(GUEST(const char *, a)));
	case __NR_readlinkat:
	{
		const char *path = GUEST(const char *, b);
		long result = proc_link(path, GUEST(char *, c), (size_t)(uint32_t)d);

		if (result != NOT_PROC)
			return result;
		return result_of(readlinkat(darwin_directory(a), path, GUEST(char *, c), (size_t)(uint32_t)d));
	}
	case __NR_utimensat:
	{
		struct timespec times[2];
		const struct guest_timespec *guest_times = GUEST(const struct guest_timespec *, c);
		int index;

		for (index = 0; guest_times && index < 2; index++)
		{
			times[index].tv_sec = guest_times[index].seconds;
			times[index].tv_nsec = guest_times[index].nanoseconds == LINUX_UTIME_NOW ? UTIME_NOW :
				guest_times[index].nanoseconds == LINUX_UTIME_OMIT ? UTIME_OMIT : guest_times[index].nanoseconds;
		}
		return result_of(utimensat(darwin_directory(a), GUEST(const char *, b), guest_times ? times : NULL,
			darwin_at_flags(d)));
	}
	case __NR_dup:
		return result_of(dup((int)a));
	case __NR_dup3:
	{
		int result = dup2((int)a, (int)b);

		if (result >= 0 && (c & LINUX_O_CLOEXEC))
			fcntl(result, F_SETFD, FD_CLOEXEC);
		return result_of(result);
	}
	case __NR_pipe2:
	{
		int descriptors[2];
		int32_t *result = GUEST(int32_t *, a);

		if (pipe(descriptors) == -1)
			return result_of(-1);
		if (b & LINUX_O_CLOEXEC)
		{
			fcntl(descriptors[0], F_SETFD, FD_CLOEXEC);
			fcntl(descriptors[1], F_SETFD, FD_CLOEXEC);
		}
		if (b & LINUX_O_NONBLOCK)
		{
			fcntl(descriptors[0], F_SETFL, O_NONBLOCK);
			fcntl(descriptors[1], F_SETFL, O_NONBLOCK);
		}
		result[0] = descriptors[0];
		result[1] = descriptors[1];
		return 0;
	}
	case __NR_ioctl:
		return -25;

	case __NR_clock_gettime:
	case __NR_clock_getres:
	{
		struct timespec value;
		clockid_t clock;

		if (!darwin_clock(a, &clock))
			return -22;
		if ((number == __NR_clock_gettime ? clock_gettime(clock, &value) : clock_getres(clock, &value)) == -1)
			return result_of(-1);
		timespec_out((uint64_t)b, &value);
		return 0;
	}
	case __NR_gettimeofday:
	{
		struct timespec value;
		struct guest_timespec *result = GUEST(struct guest_timespec *, a);

		clock_gettime(CLOCK_REALTIME, &value);
		if (result)
		{
			result->seconds = (int32_t)value.tv_sec;
			result->nanoseconds = (int32_t)(value.tv_nsec / 1000);
		}
		return 0;
	}
	case __NR_nanosleep:
	{
		struct timespec request, remaining;
		long result;

		if (!timespec_in((uint64_t)a, &request))
			return -14;
		result = result_of(nanosleep(&request, &remaining));
		if (result == -4)
			timespec_out((uint64_t)b, &remaining);
		return result;
	}
	case __NR_clock_nanosleep:
	{
		struct timespec request, remaining;
		clockid_t clock;
		long result;

		if (!timespec_in((uint64_t)c, &request))
			return -14;
		if (!darwin_clock(a, &clock))
			return -22;
		if (b & LINUX_TIMER_ABSTIME)
		{
			struct timespec now;
			uint64_t wanted = nanoseconds(&request), current;

			clock_gettime(clock, &now);
			current = nanoseconds(&now);
			if (wanted <= current)
				return 0;
			request.tv_sec = (time_t)((wanted - current) / 1000000000ULL);
			request.tv_nsec = (long)((wanted - current) % 1000000000ULL);
		}
		result = result_of(nanosleep(&request, &remaining));
		if (result == -4 && !(b & LINUX_TIMER_ABSTIME))
			timespec_out((uint64_t)d, &remaining);
		return result;
	}
	case __NR_futex:
		return futex((uint64_t)a, (int)b, (uint32_t)c, (uint64_t)d, (uint64_t)e, (uint32_t)f);
	case __NR_ppoll:
	{
		struct timespec timeout;
		int milliseconds = -1;

		if (timespec_in((uint64_t)c, &timeout))
			milliseconds = (int)(timeout.tv_sec * 1000 + (timeout.tv_nsec + 999999) / 1000000);
		/* (struct pollfd and its event bits are Linux's) */
		return result_of(poll(GUEST(struct pollfd *, a), (nfds_t)(uint32_t)b, milliseconds));
	}

	case __NR_mmap:
		return host_guest_mmap((uint64_t)a, (uint64_t)b, (int)c, (int)d, (int)e, f);
	case __NR_munmap:
		return host_guest_munmap((uint64_t)a, (uint64_t)b);
	case __NR_mprotect:
		return host_guest_mprotect((uint64_t)a, (uint64_t)b, (int)c);
	case __NR_madvise:
		/* (only "don't need" changes anything: the pages read as zero
		again, which the guest may count on) */
		if (c == LINUX_MADV_DONTNEED && host_low_owns(host_address((uint32_t)a), (size_t)(uint32_t)b))
			memset((void *)host_address((uint32_t)a), 0, (size_t)(uint32_t)b);
		return 0;
	case __NR_mremap:
	case __NR_brk:
		/* musl then falls back to mmap and copying */
		return -12;

	case __NR_exit:
	case __NR_exit_group:
		host_exit((int)a);

	case __NR_set_tid_address:
	case __NR_gettid:
	{
		uint64_t thread = 0;

		pthread_threadid_np(NULL, &thread);
		return (long)(uint32_t)thread & 0x7fffffff;
	}
	case __NR_getpid:
		return getpid();
	case __NR_getppid:
		return getppid();
	case __NR_getuid:
		return getuid();
	case __NR_geteuid:
		return geteuid();
	case __NR_getgid:
		return getgid();
	case __NR_getegid:
		return getegid();
	case __NR_umask:
		return umask((mode_t)a);
	case __NR_sched_yield:
		sched_yield();
		return 0;
	case GUEST_SYS_sched_getaffinity:
		/* (the host's processors, all of them: musl's processor count) */
		return halo_guest_affinity(GUEST(unsigned char *, c), (unsigned long)(uint32_t)b,
			sysconf(_SC_NPROCESSORS_ONLN));
	/* the calling thread's priority (who 0, as Linux sets a thread's):
	niced, the background's quality of service */
	case __NR_setpriority:
		if (a == PRIO_PROCESS && b == 0 && (int)c > 0)
			pthread_set_qos_class_self_np((int)c >= 10 ? QOS_CLASS_BACKGROUND : QOS_CLASS_UTILITY, 0);
		return 0;
	case __NR_getrandom:
		arc4random_buf(GUEST(void *, a), (size_t)(uint32_t)b);
		return (uint32_t)b;
	case __NR_kill:
	case __NR_tkill:
	case __NR_tgkill:
	{
		long long signal_number = number == __NR_tgkill ? c : b;

		/* (raise(), from abort(): the only signal the guest sends) */
		if (signal_number == LINUX_SIGABRT)
			host_abort("the guest aborted");
		return signal_number == 0 ? 0 : -38;
	}
	case __NR_rt_sigprocmask:
		if (c)
			memset(GUEST(void *, c), 0, (size_t)(uint32_t)d);
		return 0;
	case __NR_rt_sigaction:
	case __NR_sigaltstack:
		/* the host owns signal handling */
		return 0;
	case __NR_uname:
	{
		struct utsname value;
		char *result = GUEST(char *, a);

		if (uname(&value) == -1)
			return result_of(-1);
		/* Linux's: six fields of 65 bytes */
		memset(result, 0, 6 * 65);
		strncpy(result + 0 * 65, value.sysname, 64);
		strncpy(result + 1 * 65, value.nodename, 64);
		strncpy(result + 2 * 65, value.release, 64);
		strncpy(result + 3 * 65, value.version, 64);
		strncpy(result + 4 * 65, value.machine, 64);
		return 0;
	}
	case __NR_prlimit64:
	case __NR_getrlimit:
	{
		struct linux_rlimit *result = GUEST(struct linux_rlimit *, number == __NR_prlimit64 ? d : b);
		long long resource = number == __NR_prlimit64 ? b : a;
		struct rlimit value;

		if (number == __NR_prlimit64 && a)
			return -1;
		if (!result)
			return 0;
		if (resource != LINUX_RLIMIT_STACK && resource != LINUX_RLIMIT_NOFILE)
			return -22;
		if (getrlimit(resource == LINUX_RLIMIT_STACK ? RLIMIT_STACK : RLIMIT_NOFILE, &value) == -1)
			return result_of(-1);
		result->current = value.rlim_cur == RLIM_INFINITY ? ~0ULL : value.rlim_cur;
		result->maximum = value.rlim_max == RLIM_INFINITY ? ~0ULL : value.rlim_max;
		return 0;
	}
	case __NR_sysinfo:
		if (!(uint32_t)a)
			return -14; /* EFAULT */
		return guest_sysinfo(GUEST(struct halo_guest_sysinfo *, a));
	case __NR_membarrier:
	case __NR_statx:
		return -38;

	default:
	{
		static unsigned char reported[512];

		if (number >= 0 && number < 512 && !reported[number])
		{
			reported[number] = 1;
			host_logf(HOST_LOG_WARN, "guest system call %lld is not supported", number);
		}
		return -38;
	}
	}
}
