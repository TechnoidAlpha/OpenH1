/*
HOST_SYSCALL_WIN.C

The guest's system calls on Windows (port/android/host/host_syscall.c is
the Linux and Android hosts', port/macos/host_syscall.c macOS's). The
guest's musl was built for Linux: its system calls have the guest
interface's numbers, flags, structure layouts and error numbers
(halo_guest_syscall.h), which this file performs with Windows's functions,
and answers in Linux's terms.

- Descriptors are the C runtime's (its low-level I/O, <io.h>), each over a
  Windows handle, so that the posix.h functions the platform layer calls
  with them (port/windows/src/win32_files.c's posix_fstat, posix_seek,
  posix_truncate) take the same numbers; reads and writes go to the handle
  itself, in binary. What a descriptor needs beyond the C runtime's (its
  path, for the calls relative to a directory and for /proc/self/fd; a
  directory's listing; a second handle of the file for pread and pwrite)
  is kept here. A descriptor that is not open is EBADF, as on Linux: the C
  runtime's calls are made without its invalid-parameter handler, which
  would otherwise end the game (host_crash_win.c).
- Paths are UTF-8, as the guest has them (the folders the host gives it are
  UTF-8: host_main_win.c), and become Windows's UTF-16; a file is opened
  with every sharing mode, so that it can be renamed and deleted while open,
  as on Linux. /proc/self/exe is the executable, with forward slashes (the
  data folder is found from it, xbox_files.c), and /dev/null is NUL.
- Time comes from Windows's precise clocks; the guest's sleeps are
  high-resolution waitable timers; its futexes are WaitOnAddress and
  WakeByAddress.
- Memory mappings are host_memory_win.c's.
- The standard output and error streams are the process's (the console of a
  debug build); without them (a release build has no console), each line
  goes to the log.
- Calls that have no meaning for the guest (signal handlers, which the host
  owns) are answered here; a call the guest's runtime never makes fails with
  ENOSYS, logged once.

Results: the guest's system calls return a 32-bit long (guest_syscall.c):
an address above 2 GB comes back as a negative 64-bit value whose low 32
bits are the address, which the guest reads.
*/

#include "host_win.h"
#include "halo_guest_syscall.h"

#include <windows.h>
#include <bcrypt.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GUEST(type, value) ((type)host_pointer((uint32_t)(value)))

/* Linux's values the guest interface does not name */
#define LINUX_ESRCH 3
#define LINUX_EIO 5
#define LINUX_ENODEV 19
#define LINUX_EMFILE 24
#define LINUX_ENOTTY 25
#define LINUX_EXDEV 18

#define LINUX_S_IFIFO 0010000
#define LINUX_S_IFCHR 0020000
#define LINUX_S_IFDIR 0040000
#define LINUX_S_IFREG 0100000
#define LINUX_S_IFLNK 0120000

#define LINUX_DT_CHR 2
#define LINUX_DT_DIR 4
#define LINUX_DT_REG 8
#define LINUX_DT_LNK 10

#define LINUX_FUTEX_WAIT 0
#define LINUX_FUTEX_WAKE 1
#define LINUX_FUTEX_REQUEUE 3
#define LINUX_FUTEX_CMP_REQUEUE 4
#define LINUX_FUTEX_WAIT_BITSET 9
#define LINUX_FUTEX_WAKE_BITSET 10
#define LINUX_FUTEX_CMD_MASK 0x7f
#define LINUX_FUTEX_CLOCK_REALTIME 256

#define LINUX_CLOCK_REALTIME 0
#define LINUX_CLOCK_MONOTONIC 1
#define LINUX_CLOCK_PROCESS_CPUTIME_ID 2
#define LINUX_CLOCK_THREAD_CPUTIME_ID 3
#define LINUX_CLOCK_MONOTONIC_RAW 4
#define LINUX_CLOCK_REALTIME_COARSE 5
#define LINUX_CLOCK_MONOTONIC_COARSE 6
#define LINUX_CLOCK_BOOTTIME 7

#define LINUX_UTIME_NOW 0x3fffffff
#define LINUX_UTIME_OMIT 0x3ffffffe
#define LINUX_RENAME_NOREPLACE 1
#define LINUX_W_OK 2

/* ---------- descriptors */

/* (the C runtime's limit of descriptors) */
#define DESCRIPTORS_MAXIMUM 8192

struct descriptor
{
	char *path;            /* UTF-8, as opened (resolved against its directory) */
	uint32_t flags;        /* the guest's open flags (F_GETFL) */
	unsigned long access;  /* the handle's access (CreateFileW's) */
	int close_on_exec;
	int directory;
	/* pread and pwrite's handle of the same file, with a file position of its
	own (ReOpenFile), made the first time; NULL until then */
	HANDLE positional;
	/* a directory's listing (getdents64) */
	HANDLE find;
	WIN32_FIND_DATAW pending;
	int has_pending, listed;
	uint64_t entry_number;
};

static struct descriptor *descriptors[DESCRIPTORS_MAXIMUM];
static SRWLOCK descriptors_lock = SRWLOCK_INIT;

static long windows_error(void)
{
	return -host_linux_errno_of_windows(GetLastError());
}

static long crt_error(void)
{
	return -host_linux_errno(errno);
}

static HANDLE handle_of(int fd)
{
	return (HANDLE)host_crt_osfhandle(fd);
}

static int valid_handle(HANDLE handle)
{
	return handle != INVALID_HANDLE_VALUE && handle != (HANDLE)(intptr_t)-2 && handle != NULL;
}

/* (descriptors_lock held) */
static void descriptor_free(int fd)
{
	struct descriptor *entry;

	if (fd < 0 || fd >= DESCRIPTORS_MAXIMUM || !descriptors[fd])
		return;
	entry = descriptors[fd];
	if (entry->find && entry->find != INVALID_HANDLE_VALUE)
		FindClose(entry->find);
	if (entry->positional)
		CloseHandle(entry->positional);
	free(entry->path);
	free(entry);
	descriptors[fd] = NULL;
}

static void descriptor_set(int fd, const char *path, uint32_t flags, unsigned long access, int directory)
{
	struct descriptor *entry;

	if (fd < 0 || fd >= DESCRIPTORS_MAXIMUM)
		return;
	entry = calloc(1, sizeof(*entry));
	if (entry)
	{
		entry->path = path ? _strdup(path) : NULL;
		entry->flags = flags;
		entry->access = access;
		entry->directory = directory;
		entry->close_on_exec = (flags & GUEST_O_CLOEXEC) != 0;
	}
	AcquireSRWLockExclusive(&descriptors_lock);
	descriptor_free(fd);
	descriptors[fd] = entry;
	ReleaseSRWLockExclusive(&descriptors_lock);
}

/* whether a descriptor's writes go to the end of its file (O_APPEND) */
static int descriptor_appends(int fd)
{
	int appends = 0;

	AcquireSRWLockShared(&descriptors_lock);
	if (fd >= 0 && fd < DESCRIPTORS_MAXIMUM && descriptors[fd])
		appends = (descriptors[fd]->flags & GUEST_O_APPEND) != 0;
	ReleaseSRWLockShared(&descriptors_lock);
	return appends;
}

/* the descriptor's handle for pread and pwrite: a handle of the same file
with a file position of its own, which nothing else moves (a read or write
at an offset on a synchronous handle moves that handle's position: the
descriptor's own, which read, write and lseek share, stays where it was,
whatever other threads do meanwhile). NULL with -errno in *error: ESPIPE
for a pipe or a console. */
static HANDLE descriptor_positional(int fd, HANDLE handle, long *error)
{
	struct descriptor *entry;
	HANDLE positional = NULL;

	if (GetFileType(handle) != FILE_TYPE_DISK)
	{
		*error = -29; /* ESPIPE */
		return NULL;
	}
	AcquireSRWLockExclusive(&descriptors_lock);
	entry = fd >= 0 && fd < DESCRIPTORS_MAXIMUM ? descriptors[fd] : NULL;
	if (entry && !entry->positional && entry->access)
	{
		entry->positional = ReOpenFile(handle, entry->access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			0);
		if (entry->positional == INVALID_HANDLE_VALUE)
		{
			*error = windows_error();
			entry->positional = NULL;
		}
	}
	else if (!entry || !entry->access)
	{
		*error = -GUEST_EBADF;
	}
	if (entry)
		positional = entry->positional;
	ReleaseSRWLockExclusive(&descriptors_lock);
	return positional;
}

/* whether a descriptor was opened for writing alone (a pipe's write end) */
static int descriptor_writes_only(int fd)
{
	int writes_only = 0;

	AcquireSRWLockShared(&descriptors_lock);
	if (fd >= 0 && fd < DESCRIPTORS_MAXIMUM && descriptors[fd])
		writes_only = (descriptors[fd]->flags & GUEST_O_ACCMODE) == GUEST_O_WRONLY;
	ReleaseSRWLockShared(&descriptors_lock);
	return writes_only;
}

/* a copy of a descriptor's path (free it); NULL if it has none */
static char *descriptor_path(int fd)
{
	char *path = NULL;

	AcquireSRWLockShared(&descriptors_lock);
	if (fd >= 0 && fd < DESCRIPTORS_MAXIMUM && descriptors[fd] && descriptors[fd]->path)
		path = _strdup(descriptors[fd]->path);
	ReleaseSRWLockShared(&descriptors_lock);
	return path;
}

/* ---------- paths */

static int absolute_path(const char *path)
{
	return path[0] == '/' || path[0] == '\\' ||
		(((path[0] | 0x20) >= 'a' && (path[0] | 0x20) <= 'z') && path[1] == ':');
}

/* the UTF-16 path of a guest path relative to a directory descriptor (or
AT_FDCWD), with Windows's separators; NULL with -errno in *error */
static wchar_t *resolve(int directory, const char *path, long *error)
{
	char *joined = NULL;
	wchar_t *wide, *at;

	if (!path)
	{
		*error = -GUEST_EFAULT;
		return NULL;
	}
	if (!strcmp(path, "/dev/null"))
		path = "NUL";
	if (!path[0])
	{
		*error = -GUEST_ENOENT;
		return NULL;
	}
	if (directory != GUEST_AT_FDCWD && !absolute_path(path))
	{
		char *base = descriptor_path(directory);
		size_t size;

		if (!base)
		{
			*error = -GUEST_EBADF;
			return NULL;
		}
		size = strlen(base) + strlen(path) + 2;
		joined = malloc(size);
		if (!joined)
		{
			free(base);
			*error = -GUEST_ENOMEM;
			return NULL;
		}
		snprintf(joined, size, "%s\\%s", base, path);
		free(base);
		path = joined;
	}
	wide = host_wide_from_utf8(path);
	free(joined);
	if (!wide)
	{
		*error = -GUEST_ENOMEM;
		return NULL;
	}
	for (at = wide; *at; at++)
	{
		if (*at == L'/')
			*at = L'\\';
	}
	return wide;
}

/* the UTF-8 path a descriptor keeps for a resolved path */
static char *resolved_utf8(const wchar_t *wide)
{
	return host_utf8_from_wide(wide);
}

/* ---------- structures */

static int timespec_in(uint64_t address, int64_t *seconds, int64_t *nanoseconds)
{
	const struct halo_guest_timespec *value = GUEST(const struct halo_guest_timespec *, address);

	if (!address)
		return 0;
	*seconds = value->seconds;
	*nanoseconds = value->nanoseconds;
	return 1;
}

static void timespec_out(uint64_t address, int64_t seconds, int64_t nanoseconds)
{
	struct halo_guest_timespec *result = GUEST(struct halo_guest_timespec *, address);

	if (!address)
		return;
	result->seconds = (int32_t)seconds;
	result->nanoseconds = (int32_t)nanoseconds;
}

static void filetime_out(const FILETIME *time, int64_t *seconds, int64_t *nanoseconds)
{
	host_unix_time_of_filetime(((uint64_t)time->dwHighDateTime << 32) | time->dwLowDateTime, seconds, nanoseconds);
}

static FILETIME filetime_in(int64_t seconds, int64_t nanoseconds)
{
	uint64_t value = host_filetime_of_unix_time(seconds, nanoseconds);
	FILETIME result;

	result.dwLowDateTime = (DWORD)value;
	result.dwHighDateTime = (DWORD)(value >> 32);
	return result;
}

/* the guest's kernel stat of an open handle */
static long stat_handle(HANDLE handle, uint64_t address)
{
	struct halo_guest_kstat *result = GUEST(struct halo_guest_kstat *, address);
	BY_HANDLE_FILE_INFORMATION information;
	DWORD type = GetFileType(handle);

	if (!address)
		return -GUEST_EFAULT;
	memset(result, 0, sizeof(*result));
	result->st_nlink = 1;
	result->st_blksize = 4096;
	if (type == FILE_TYPE_CHAR || type == FILE_TYPE_PIPE)
	{
		result->st_mode = (type == FILE_TYPE_CHAR ? LINUX_S_IFCHR : LINUX_S_IFIFO) | 0600;
		return 0;
	}
	if (!GetFileInformationByHandle(handle, &information))
		return windows_error();
	result->st_dev = information.dwVolumeSerialNumber;
	result->st_ino = ((uint64_t)information.nFileIndexHigh << 32) | information.nFileIndexLow;
	result->st_nlink = information.nNumberOfLinks;
	if (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		result->st_mode = LINUX_S_IFDIR | 0755;
	else if (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
		result->st_mode = LINUX_S_IFLNK | 0777;
	else
		result->st_mode = LINUX_S_IFREG | ((information.dwFileAttributes & FILE_ATTRIBUTE_READONLY) ? 0444 : 0644);
	result->st_size = (int64_t)(((uint64_t)information.nFileSizeHigh << 32) | information.nFileSizeLow);
	result->st_blocks = (result->st_size + 511) / 512;
	filetime_out(&information.ftLastAccessTime, &result->atime_seconds, &result->atime_nanoseconds);
	filetime_out(&information.ftLastWriteTime, &result->mtime_seconds, &result->mtime_nanoseconds);
	filetime_out(&information.ftCreationTime, &result->ctime_seconds, &result->ctime_nanoseconds);
	return 0;
}

/* opens a path for its attributes alone (any file or directory) */
static HANDLE open_for_attributes(const wchar_t *path, DWORD access, int no_follow)
{
	return CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | (no_follow ? FILE_FLAG_OPEN_REPARSE_POINT : 0), NULL);
}

static long guest_fstatat(int directory, uint64_t path_address, uint64_t result, int flags)
{
	const char *path = GUEST(const char *, path_address);
	wchar_t *wide;
	HANDLE handle;
	long error = 0, status;

	if ((flags & GUEST_AT_EMPTY_PATH) && (!path || !path[0]))
	{
		if (directory == GUEST_AT_FDCWD)
		{
			path = ".";
		}
		else
		{
			handle = handle_of(directory);
			return valid_handle(handle) ? stat_handle(handle, result) : -GUEST_EBADF;
		}
	}
	wide = resolve(directory, path, &error);
	if (!wide)
		return error;
	handle = open_for_attributes(wide, FILE_READ_ATTRIBUTES, (flags & GUEST_AT_SYMLINK_NOFOLLOW) != 0);
	free(wide);
	if (handle == INVALID_HANDLE_VALUE)
		return windows_error();
	status = stat_handle(handle, result);
	CloseHandle(handle);
	return status;
}

/* struct sysinfo in the guest's layout: a size too large for 32 bits is
counted in larger units (mem_unit), as a 64-bit kernel does for its 32-bit
programs */
static uint32_t saturate(uint64_t value)
{
	return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static long guest_sysinfo(uint64_t address)
{
	struct halo_guest_sysinfo *result = GUEST(struct halo_guest_sysinfo *, address);
	MEMORYSTATUSEX memory;
	uint64_t sizes[4];
	uint64_t unit = 1;
	int index, large;

	if (!address)
		return -GUEST_EFAULT;
	memory.dwLength = sizeof(memory);
	if (!GlobalMemoryStatusEx(&memory))
		return windows_error();
	sizes[0] = memory.ullTotalPhys;
	sizes[1] = memory.ullAvailPhys;
	sizes[2] = memory.ullTotalPageFile > memory.ullTotalPhys ? memory.ullTotalPageFile - memory.ullTotalPhys : 0;
	sizes[3] = memory.ullAvailPageFile > memory.ullAvailPhys ? memory.ullAvailPageFile - memory.ullAvailPhys : 0;
	for (;;)
	{
		large = 0;
		for (index = 0; index < 4; index++)
			large |= sizes[index] > UINT32_MAX;
		if (!large || unit >= 0x80000000u)
			break;
		unit <<= 1;
		for (index = 0; index < 4; index++)
			sizes[index] >>= 1;
	}
	memset(result, 0, sizeof(*result));
	result->uptime = saturate(GetTickCount64() / 1000);
	result->totalram = saturate(sizes[0]);
	result->freeram = saturate(sizes[1]);
	result->totalswap = saturate(sizes[2]);
	result->freeswap = saturate(sizes[3]);
	result->procs = 1;
	result->mem_unit = (uint32_t)unit;
	return 0;
}

/* ---------- standard output and error, without a console: the log */

struct log_stream
{
	char line[1024];
	size_t length;
};

static struct log_stream log_streams[2];
static SRWLOCK log_lock = SRWLOCK_INIT;

static void log_bytes(int fd, const char *bytes, size_t size)
{
	struct log_stream *stream = &log_streams[fd == 2];
	size_t index;

	AcquireSRWLockExclusive(&log_lock);
	for (index = 0; index < size; index++)
	{
		char c = bytes[index];

		if (c == '\n' || stream->length == sizeof(stream->line) - 1)
		{
			stream->line[stream->length] = 0;
			host_log(fd == 2 ? HOST_LOG_WARN : HOST_LOG_INFO, stream->line);
			stream->length = 0;
			if (c == '\n')
				continue;
		}
		if (c != '\r')
			stream->line[stream->length++] = c;
	}
	ReleaseSRWLockExclusive(&log_lock);
}

/* ---------- reads and writes */

static long write_handle(int fd, const void *buffer, uint32_t size, int64_t offset, int positional)
{
	HANDLE handle = handle_of(fd);
	OVERLAPPED overlapped;
	DWORD written = 0;
	long error = 0;

	if (!valid_handle(handle))
	{
		if (fd == 1 || fd == 2)
		{
			log_bytes(fd, buffer, size);
			return size;
		}
		return -GUEST_EBADF;
	}
	if (!positional)
	{
		OVERLAPPED *at_end = NULL;

		if (descriptor_appends(fd))
		{
			/* (O_APPEND: at the end, as one write; the file's position
			then follows it, as Linux's does) */
			memset(&overlapped, 0, sizeof(overlapped));
			overlapped.Offset = overlapped.OffsetHigh = 0xffffffff;
			at_end = &overlapped;
		}
		if (!WriteFile(handle, buffer, size, &written, at_end))
			return GetLastError() == ERROR_NO_DATA ? -32 /* EPIPE */ : windows_error();
		return (long)written;
	}
	/* (pwrite: at the offset, through the descriptor's second handle, so
	that its own position stays where it is) */
	if (offset < 0)
		return -GUEST_EINVAL;
	handle = descriptor_positional(fd, handle, &error);
	if (!handle)
		return error;
	memset(&overlapped, 0, sizeof(overlapped));
	overlapped.Offset = (DWORD)offset;
	overlapped.OffsetHigh = (DWORD)((uint64_t)offset >> 32);
	return WriteFile(handle, buffer, size, &written, &overlapped) ? (long)written : windows_error();
}

static long read_handle(int fd, void *buffer, uint32_t size, int64_t offset, int positional)
{
	HANDLE handle = handle_of(fd);
	OVERLAPPED overlapped;
	DWORD read = 0;
	long error = 0;

	if (!valid_handle(handle))
		return -GUEST_EBADF;
	/* (into pages the write tracking protects: Windows would refuse them) */
	host_memory_watch_prepare_write(guest_address(buffer), size);
	if (!positional)
	{
		if (!ReadFile(handle, buffer, size, &read, NULL))
			return GetLastError() == ERROR_BROKEN_PIPE || GetLastError() == ERROR_HANDLE_EOF ? 0 : windows_error();
		return (long)read;
	}
	/* (pread: as pwrite) */
	if (offset < 0)
		return -GUEST_EINVAL;
	handle = descriptor_positional(fd, handle, &error);
	if (!handle)
		return error;
	memset(&overlapped, 0, sizeof(overlapped));
	overlapped.Offset = (DWORD)offset;
	overlapped.OffsetHigh = (DWORD)((uint64_t)offset >> 32);
	if (!ReadFile(handle, buffer, size, &read, &overlapped))
		return GetLastError() == ERROR_HANDLE_EOF ? 0 : windows_error();
	return (long)read;
}

/* a 64-bit offset from the two longs musl passes for it, low half first */
static int64_t offset_of_halves(long long low, long long high)
{
	return (int64_t)((uint64_t)(uint32_t)low | ((uint64_t)(uint32_t)high << 32));
}

/* readv, writev and their positional forms, one vector at a time */
static long guest_vector(int fd, uint64_t vector, int count, int64_t offset, int positional, int write)
{
	const struct halo_guest_iovec *guest = GUEST(const struct halo_guest_iovec *, vector);
	long total = 0;
	int index;

	if (count < 0 || count > GUEST_IOV_MAX)
		return -GUEST_EINVAL;
	if (count && !vector)
		return -GUEST_EFAULT;
	for (index = 0; index < count; index++)
	{
		void *base = GUEST(void *, guest[index].base);
		uint32_t length = guest[index].length;
		long done;

		if (!length)
			continue;
		done = write ? write_handle(fd, base, length, offset + total, positional) :
			read_handle(fd, base, length, offset + total, positional);
		if (done < 0)
			return total ? total : done;
		total += done;
		if ((uint32_t)done < length)
			break;
	}
	return total;
}

/* ---------- files */

static long guest_openat(int directory, uint64_t path_address, uint32_t flags, uint32_t mode)
{
	const char *path = GUEST(const char *, path_address);
	DWORD access, disposition, attributes = FILE_ATTRIBUTE_NORMAL;
	BY_HANDLE_FILE_INFORMATION information;
	int crt_flags = _O_BINARY | _O_NOINHERIT;
	wchar_t *wide;
	char *utf8;
	HANDLE handle;
	long error = 0;
	int fd, is_directory;

	if ((flags & GUEST_O_TMPFILE) == GUEST_O_TMPFILE || (flags & GUEST_O_PATH))
		return -GUEST_EOPNOTSUPP;
	switch (flags & GUEST_O_ACCMODE)
	{
	case GUEST_O_RDONLY:
		access = GENERIC_READ;
		crt_flags |= _O_RDONLY;
		break;
	case GUEST_O_WRONLY:
		access = GENERIC_WRITE;
		crt_flags |= _O_WRONLY;
		break;
	default:
		access = GENERIC_READ | GENERIC_WRITE;
		crt_flags |= _O_RDWR;
		break;
	}
	if ((flags & GUEST_O_CREAT) && (flags & GUEST_O_EXCL))
		disposition = CREATE_NEW;
	else if ((flags & GUEST_O_CREAT) && (flags & GUEST_O_TRUNC))
		disposition = CREATE_ALWAYS;
	else if (flags & GUEST_O_CREAT)
		disposition = OPEN_ALWAYS;
	else if (flags & GUEST_O_TRUNC)
		disposition = TRUNCATE_EXISTING;
	else
		disposition = OPEN_EXISTING;
	if ((flags & GUEST_O_CREAT) && !(mode & 0222))
		attributes = FILE_ATTRIBUTE_READONLY;
	wide = resolve(directory, path, &error);
	if (!wide)
		return error;
	handle = CreateFileW(wide, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, disposition,
		attributes | FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		DWORD failure = GetLastError();
		DWORD found = INVALID_FILE_ATTRIBUTES;

		/* (a directory opened for writing: Linux's EISDIR) */
		if (failure == ERROR_ACCESS_DENIED && (flags & GUEST_O_ACCMODE) != GUEST_O_RDONLY)
			found = GetFileAttributesW(wide);
		free(wide);
		if (found != INVALID_FILE_ATTRIBUTES && (found & FILE_ATTRIBUTE_DIRECTORY))
			return -GUEST_EISDIR;
		return -host_linux_errno_of_windows(failure);
	}
	is_directory = GetFileInformationByHandle(handle, &information) &&
		(information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
	if ((flags & GUEST_O_DIRECTORY) && !is_directory)
	{
		CloseHandle(handle);
		free(wide);
		return -GUEST_ENOTDIR;
	}
	fd = _open_osfhandle((intptr_t)handle, crt_flags);
	if (fd < 0)
	{
		CloseHandle(handle);
		free(wide);
		return -LINUX_EMFILE;
	}
	utf8 = resolved_utf8(wide);
	free(wide);
	descriptor_set(fd, utf8, flags, access, is_directory);
	free(utf8);
	return fd;
}

static long guest_close(int fd)
{
	AcquireSRWLockExclusive(&descriptors_lock);
	descriptor_free(fd);
	ReleaseSRWLockExclusive(&descriptors_lock);
	return host_crt_close(fd) == 0 ? 0 : -GUEST_EBADF;
}

/* lseek(fd, 0, SEEK_SET) of a directory (musl's rewinddir): its listing
starts again; 1 if fd is a directory (another seek of one is a no-op that
reports 0, as getdents64 keeps its own place) */
static int guest_directory_rewind(int fd, uint32_t offset_high, uint32_t offset_low, int whence)
{
	struct descriptor *entry;
	int directory = 0;

	AcquireSRWLockExclusive(&descriptors_lock);
	entry = fd >= 0 && fd < DESCRIPTORS_MAXIMUM ? descriptors[fd] : NULL;
	if (entry && entry->directory)
	{
		directory = 1;
		if (whence == GUEST_SEEK_SET && !offset_high && !offset_low)
		{
			if (entry->find && entry->find != INVALID_HANDLE_VALUE)
				FindClose(entry->find);
			entry->find = NULL;
			entry->has_pending = 0;
			entry->listed = 0;
			entry->entry_number = 0;
		}
	}
	ReleaseSRWLockExclusive(&descriptors_lock);
	return directory;
}

static long guest_llseek(int fd, uint32_t offset_high, uint32_t offset_low, uint64_t result, int whence)
{
	HANDLE handle = handle_of(fd);
	LARGE_INTEGER distance, position;
	DWORD method;

	if (!result)
		return -GUEST_EFAULT;
	if (!valid_handle(handle))
		return -GUEST_EBADF;
	if (guest_directory_rewind(fd, offset_high, offset_low, whence))
	{
		memset(GUEST(void *, result), 0, sizeof(int64_t));
		return 0;
	}
	switch (whence)
	{
	case GUEST_SEEK_SET: method = FILE_BEGIN; break;
	case GUEST_SEEK_CUR: method = FILE_CURRENT; break;
	case GUEST_SEEK_END: method = FILE_END; break;
	default: return -GUEST_EINVAL;
	}
	distance.QuadPart = (LONGLONG)(((uint64_t)offset_high << 32) | offset_low);
	if (!SetFilePointerEx(handle, distance, &position, method))
		return windows_error();
	memcpy(GUEST(void *, result), &position.QuadPart, sizeof(int64_t));
	return 0;
}

/* getdents64: struct linux_dirent64 records (ino and off, 8 bytes each;
reclen, 2; type, 1; the name, NUL-terminated, the record 8-byte aligned) */
static long guest_getdents64(int fd, unsigned char *buffer, uint32_t size)
{
	struct descriptor *entry;
	uint32_t used = 0;
	long status = 0;

	AcquireSRWLockExclusive(&descriptors_lock);
	entry = fd >= 0 && fd < DESCRIPTORS_MAXIMUM ? descriptors[fd] : NULL;
	if (!entry || !entry->directory || !entry->path)
	{
		status = entry ? -GUEST_ENOTDIR : -GUEST_EBADF;
		goto done;
	}
	if (!entry->find)
	{
		size_t length = strlen(entry->path);
		char *pattern = malloc(length + 3);
		wchar_t *wide;

		if (!pattern)
		{
			status = -GUEST_ENOMEM;
			goto done;
		}
		snprintf(pattern, length + 3, "%s%s*", entry->path,
			length && (entry->path[length - 1] == '\\' || entry->path[length - 1] == '/') ? "" : "\\");
		wide = host_wide_from_utf8(pattern);
		free(pattern);
		entry->find = wide ? FindFirstFileW(wide, &entry->pending) : INVALID_HANDLE_VALUE;
		free(wide);
		entry->has_pending = entry->find != INVALID_HANDLE_VALUE;
		if (!entry->has_pending)
			entry->listed = 1;
	}
	while (!entry->listed)
	{
		char name[MAX_PATH * 3];
		size_t length;
		uint32_t record;
		unsigned char type;
		uint64_t number;

		if (!entry->has_pending)
		{
			if (!FindNextFileW(entry->find, &entry->pending))
			{
				entry->listed = 1;
				break;
			}
			entry->has_pending = 1;
		}
		if (!WideCharToMultiByte(CP_UTF8, 0, entry->pending.cFileName, -1, name, (int)sizeof(name), NULL, NULL))
		{
			entry->has_pending = 0;
			continue;
		}
		length = strlen(name);
		record = (uint32_t)((19 + length + 1 + 7) & ~7u);
		if (used + record > size)
		{
			if (!used)
				status = -GUEST_EINVAL;
			break;
		}
		type = (entry->pending.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? LINUX_DT_DIR :
			(entry->pending.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? LINUX_DT_LNK : LINUX_DT_REG;
		/* (an inode number of its own, never 0) */
		number = ++entry->entry_number;
		memset(buffer + used, 0, record);
		memcpy(buffer + used, &number, 8);
		memcpy(buffer + used + 8, &(int64_t){ (int64_t)(used + record) }, 8);
		memcpy(buffer + used + 16, &(uint16_t){ (uint16_t)record }, 2);
		buffer[used + 18] = type;
		memcpy(buffer + used + 19, name, length);
		used += record;
		entry->has_pending = 0;
	}
	if (!status)
		status = used;
done:
	ReleaseSRWLockExclusive(&descriptors_lock);
	return status;
}

static long guest_unlinkat(int directory, uint64_t path, int flags)
{
	long error = 0;
	wchar_t *wide = resolve(directory, GUEST(const char *, path), &error);
	BOOL result;

	if (!wide)
		return error;
	if (flags & GUEST_AT_REMOVEDIR)
	{
		result = RemoveDirectoryW(wide);
	}
	else
	{
		result = DeleteFileW(wide);
		if (!result && GetLastError() == ERROR_ACCESS_DENIED)
		{
			/* (Linux deletes a read-only file: its folder's permission
			counts, not its own) */
			DWORD attributes = GetFileAttributesW(wide);

			if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				free(wide);
				return -GUEST_EISDIR;
			}
			if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) &&
				SetFileAttributesW(wide, attributes & ~FILE_ATTRIBUTE_READONLY))
			{
				result = DeleteFileW(wide);
				if (!result)
					SetFileAttributesW(wide, attributes);
			}
			else
			{
				SetLastError(ERROR_ACCESS_DENIED);
			}
		}
	}
	free(wide);
	return result ? 0 : windows_error();
}

static long guest_renameat(int old_directory, uint64_t old_path, int new_directory, uint64_t new_path,
	uint32_t flags)
{
	long error = 0;
	wchar_t *from, *to;
	BOOL result;

	if (flags & ~(uint32_t)LINUX_RENAME_NOREPLACE)
		return -GUEST_EINVAL;
	from = resolve(old_directory, GUEST(const char *, old_path), &error);
	if (!from)
		return error;
	to = resolve(new_directory, GUEST(const char *, new_path), &error);
	if (!to)
	{
		free(from);
		return error;
	}
	result = MoveFileExW(from, to, (flags & LINUX_RENAME_NOREPLACE) ? 0 : MOVEFILE_REPLACE_EXISTING);
	free(from);
	free(to);
	if (!result)
		return GetLastError() == ERROR_NOT_SAME_DEVICE ? -LINUX_EXDEV : windows_error();
	return 0;
}

static long guest_mkdirat(int directory, uint64_t path)
{
	long error = 0;
	wchar_t *wide = resolve(directory, GUEST(const char *, path), &error);
	BOOL result;

	if (!wide)
		return error;
	result = CreateDirectoryW(wide, NULL);
	free(wide);
	return result ? 0 : windows_error();
}

/* the read-only attribute for a mode without write permission */
static long set_mode(const wchar_t *path, uint32_t mode)
{
	DWORD attributes = GetFileAttributesW(path);

	if (attributes == INVALID_FILE_ATTRIBUTES)
		return windows_error();
	if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
	{
		attributes = (mode & 0222) ? attributes & ~FILE_ATTRIBUTE_READONLY : attributes | FILE_ATTRIBUTE_READONLY;
		if (!SetFileAttributesW(path, attributes ? attributes : FILE_ATTRIBUTE_NORMAL))
			return windows_error();
	}
	return 0;
}

static long guest_fchmodat(int directory, uint64_t path, uint32_t mode)
{
	long error = 0;
	wchar_t *wide = resolve(directory, GUEST(const char *, path), &error);

	if (!wide)
		return error;
	error = set_mode(wide, mode);
	free(wide);
	return error;
}

static long guest_fchmod(int fd, uint32_t mode)
{
	char *path = descriptor_path(fd);
	wchar_t *wide;
	long status;

	if (!path)
		return -GUEST_EBADF;
	wide = host_wide_from_utf8(path);
	status = wide ? set_mode(wide, mode) : -GUEST_ENOMEM;
	free(wide);
	free(path);
	return status;
}

static long guest_faccessat(int directory, uint64_t path, int mode)
{
	long error = 0;
	wchar_t *wide = resolve(directory, GUEST(const char *, path), &error);
	DWORD attributes;

	if (!wide)
		return error;
	attributes = GetFileAttributesW(wide);
	free(wide);
	if (attributes == INVALID_FILE_ATTRIBUTES)
		return windows_error();
	if ((mode & LINUX_W_OK) && (attributes & FILE_ATTRIBUTE_READONLY) && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
		return -GUEST_EACCES;
	return 0;
}

static long guest_ftruncate(int fd, int64_t length)
{
	HANDLE handle = handle_of(fd);
	FILE_END_OF_FILE_INFO end;

	if (!valid_handle(handle))
		return -GUEST_EBADF;
	if (length < 0)
		return -GUEST_EINVAL;
	end.EndOfFile.QuadPart = length;
	return SetFileInformationByHandle(handle, FileEndOfFileInfo, &end, sizeof(end)) ? 0 : windows_error();
}

static long guest_fsync(int fd)
{
	HANDLE handle = handle_of(fd);

	if (!valid_handle(handle))
		return -GUEST_EBADF;
	/* (a file opened for reading alone cannot be flushed, and needs not) */
	if (!FlushFileBuffers(handle) && GetLastError() != ERROR_ACCESS_DENIED && GetLastError() != ERROR_INVALID_HANDLE)
		return windows_error();
	return 0;
}

static long guest_getcwd(uint64_t address, uint32_t size)
{
	char *buffer = GUEST(char *, address);
	DWORD length = GetCurrentDirectoryW(0, NULL);
	wchar_t *wide;
	char *utf8;
	size_t utf8_length;

	if (!length)
		return windows_error();
	wide = malloc(length * sizeof(wchar_t));
	if (!wide)
		return -GUEST_ENOMEM;
	GetCurrentDirectoryW(length, wide);
	utf8 = host_utf8_from_wide(wide);
	free(wide);
	if (!utf8)
		return -GUEST_ENOMEM;
	utf8_length = strlen(utf8);
	if (utf8_length + 1 > size)
	{
		free(utf8);
		return -GUEST_ERANGE;
	}
	memcpy(buffer, utf8, utf8_length + 1);
	free(utf8);
	return (long)utf8_length + 1;
}

static long guest_chdir(uint64_t path)
{
	long error = 0;
	wchar_t *wide = resolve(GUEST_AT_FDCWD, GUEST(const char *, path), &error);
	BOOL result;

	if (!wide)
		return error;
	result = SetCurrentDirectoryW(wide);
	free(wide);
	return result ? 0 : windows_error();
}

/* readlink: /proc/self/exe and /proc/self/fd/N, which musl and the platform
layer read; Windows's links are not read as links (EINVAL) */
static long guest_readlinkat(int directory, uint64_t path_address, uint64_t buffer_address, uint32_t size)
{
	const char *path = GUEST(const char *, path_address);
	char *buffer = GUEST(char *, buffer_address);
	char *target = NULL;
	const char *text = NULL;
	size_t length;
	int fd;

	(void)directory;
	if (!path || !buffer)
		return -GUEST_EFAULT;
	if (!strcmp(path, "/proc/self/exe"))
		text = host_executable_path();
	else if (sscanf(path, "/proc/self/fd/%d", &fd) == 1)
		text = target = descriptor_path(fd);
	if (!text || !text[0])
	{
		free(target);
		return text ? -GUEST_ENOENT : -GUEST_EINVAL;
	}
	length = strlen(text);
	if (length > size)
		length = size;
	memcpy(buffer, text, length);
	free(target);
	return (long)length;
}

static long guest_utimensat(int directory, uint64_t path, uint64_t times, int flags)
{
	FILETIME access, modification, now;
	FILETIME *access_pointer = &access, *modification_pointer = &modification;
	HANDLE handle;
	long error = 0;
	int owned = 0;

	GetSystemTimePreciseAsFileTime(&now);
	access = modification = now;
	if (times)
	{
		int64_t seconds[2], nanoseconds[2];
		FILETIME *pointers[2] = { &access, &modification };
		FILETIME **results[2] = { &access_pointer, &modification_pointer };
		int index;

		timespec_in(times, &seconds[0], &nanoseconds[0]);
		timespec_in(times + sizeof(struct halo_guest_timespec), &seconds[1], &nanoseconds[1]);
		for (index = 0; index < 2; index++)
		{
			if (nanoseconds[index] == LINUX_UTIME_OMIT)
				*results[index] = NULL;
			else if (nanoseconds[index] != LINUX_UTIME_NOW)
				*pointers[index] = filetime_in(seconds[index], nanoseconds[index]);
		}
	}
	if (!path)
	{
		handle = handle_of(directory);
		if (!valid_handle(handle))
			return -GUEST_EBADF;
	}
	else
	{
		wchar_t *wide = resolve(directory, GUEST(const char *, path), &error);

		if (!wide)
			return error;
		handle = open_for_attributes(wide, FILE_WRITE_ATTRIBUTES, (flags & GUEST_AT_SYMLINK_NOFOLLOW) != 0);
		free(wide);
		if (handle == INVALID_HANDLE_VALUE)
			return windows_error();
		owned = 1;
	}
	error = SetFileTime(handle, NULL, access_pointer, modification_pointer) ? 0 : windows_error();
	if (owned)
		CloseHandle(handle);
	return error;
}

static long guest_dup3(int from, int to, uint32_t flags, int dup2)
{
	char *path = NULL;
	uint32_t from_flags = 0;
	unsigned long from_access = 0;
	int has, directory = 0;

	if (!dup2 && from == to)
		return -GUEST_EINVAL;
	if (host_crt_dup2(from, to) != 0)
		return crt_error();
	AcquireSRWLockShared(&descriptors_lock);
	has = from >= 0 && from < DESCRIPTORS_MAXIMUM && descriptors[from];
	if (has)
	{
		path = descriptors[from]->path ? _strdup(descriptors[from]->path) : NULL;
		from_flags = descriptors[from]->flags;
		from_access = descriptors[from]->access;
		directory = descriptors[from]->directory;
	}
	ReleaseSRWLockShared(&descriptors_lock);
	descriptor_set(to, path, has ? (from_flags & ~(uint32_t)GUEST_O_CLOEXEC) | (flags & GUEST_O_CLOEXEC) : flags,
		from_access, directory);
	free(path);
	return to;
}

static long guest_dup(int fd, int minimum)
{
	int copies[64], count = 0, result;
	long status;

	result = host_crt_dup(fd);
	while (result >= 0 && result < minimum && count < 64)
	{
		copies[count++] = result;
		result = host_crt_dup(fd);
	}
	while (count)
		host_crt_close(copies[--count]);
	if (result < 0)
		return crt_error();
	if (result < minimum)
	{
		host_crt_close(result);
		return -LINUX_EMFILE;
	}
	status = guest_dup3(fd, result, 0, 1);
	return status;
}

static long guest_pipe2(uint64_t address, uint32_t flags)
{
	int32_t *result = GUEST(int32_t *, address);
	int pipe[2];

	if (!address)
		return -GUEST_EFAULT;
	if (_pipe(pipe, 65536, _O_BINARY | _O_NOINHERIT) != 0)
		return crt_error();
	descriptor_set(pipe[0], NULL, GUEST_O_RDONLY | (flags & GUEST_O_CLOEXEC), GENERIC_READ, 0);
	descriptor_set(pipe[1], NULL, GUEST_O_WRONLY | (flags & GUEST_O_CLOEXEC), GENERIC_WRITE, 0);
	result[0] = pipe[0];
	result[1] = pipe[1];
	return 0;
}

/* flock's advisory locks: a byte far past any file's end, which Windows's
locks (which keep others from reading what they cover) leave readable */
#define FLOCK_OFFSET_HIGH 0x40000000u

static long guest_flock(int fd, int operation)
{
	HANDLE handle = handle_of(fd);
	OVERLAPPED overlapped;

	if (!valid_handle(handle))
		return -GUEST_EBADF;
	memset(&overlapped, 0, sizeof(overlapped));
	overlapped.OffsetHigh = FLOCK_OFFSET_HIGH;
	if (operation & GUEST_LOCK_UN)
	{
		UnlockFileEx(handle, 0, 1, 0, &overlapped);
		return 0;
	}
	if (!(operation & (GUEST_LOCK_SH | GUEST_LOCK_EX)))
		return -GUEST_EINVAL;
	/* (a lock taken again, of another kind, replaces the first, as
	flock's does) */
	UnlockFileEx(handle, 0, 1, 0, &overlapped);
	if (!LockFileEx(handle, ((operation & GUEST_LOCK_EX) ? LOCKFILE_EXCLUSIVE_LOCK : 0) |
		((operation & GUEST_LOCK_NB) ? LOCKFILE_FAIL_IMMEDIATELY : 0), 0, 1, 0, &overlapped))
	{
		return GetLastError() == ERROR_LOCK_VIOLATION ? -GUEST_EAGAIN : windows_error();
	}
	return 0;
}

static long guest_fcntl(int fd, int command, uint64_t argument)
{
	struct descriptor *entry;
	long status = 0;

	if (!valid_handle(handle_of(fd)))
		return -GUEST_EBADF;
	switch (command)
	{
	case GUEST_F_DUPFD:
	case GUEST_F_DUPFD_CLOEXEC:
		status = guest_dup(fd, (int)argument);
		if (status >= 0 && command == GUEST_F_DUPFD_CLOEXEC)
			guest_fcntl((int)status, GUEST_F_SETFD, GUEST_FD_CLOEXEC);
		return status;
	case GUEST_F_GETFD:
	case GUEST_F_SETFD:
	case GUEST_F_GETFL:
	case GUEST_F_SETFL:
		AcquireSRWLockExclusive(&descriptors_lock);
		entry = fd < DESCRIPTORS_MAXIMUM ? descriptors[fd] : NULL;
		if (command == GUEST_F_GETFD)
			status = entry && entry->close_on_exec ? GUEST_FD_CLOEXEC : 0;
		else if (command == GUEST_F_SETFD && entry)
			entry->close_on_exec = ((int)argument & GUEST_FD_CLOEXEC) != 0;
		else if (command == GUEST_F_GETFL)
			status = entry ? (long)(entry->flags & ~(uint32_t)GUEST_O_CLOEXEC) : GUEST_O_RDWR;
		else if (command == GUEST_F_SETFL && entry)
			entry->flags = (entry->flags & ~(uint32_t)(GUEST_O_APPEND | GUEST_O_NONBLOCK)) |
				((uint32_t)argument & (GUEST_O_APPEND | GUEST_O_NONBLOCK));
		ReleaseSRWLockExclusive(&descriptors_lock);
		return status;
	case GUEST_F_GETLK:
	case GUEST_F_OFD_GETLK:
	{
		struct halo_guest_flock *lock = GUEST(struct halo_guest_flock *, argument);

		/* (no one else's lock is known) */
		if (!lock)
			return -GUEST_EFAULT;
		lock->l_type = GUEST_F_UNLCK;
		return 0;
	}
	case GUEST_F_SETLK:
	case GUEST_F_SETLKW:
	case GUEST_F_OFD_SETLK:
	case GUEST_F_OFD_SETLKW:
		/* (record locks are granted: one process plays) */
		return 0;
	}
	return -GUEST_EINVAL;
}

/* ---------- time */

/* the clock of the shim (port/windows/host/posix/time.h) for Linux's */
static int clock_of(long long clock, clockid_t *result)
{
	switch (clock)
	{
	case LINUX_CLOCK_REALTIME:
	case LINUX_CLOCK_REALTIME_COARSE:
		*result = CLOCK_REALTIME;
		return 1;
	case LINUX_CLOCK_MONOTONIC:
	case LINUX_CLOCK_MONOTONIC_RAW:
	case LINUX_CLOCK_MONOTONIC_COARSE:
	case LINUX_CLOCK_BOOTTIME:
		*result = CLOCK_MONOTONIC;
		return 1;
	case LINUX_CLOCK_PROCESS_CPUTIME_ID:
		*result = CLOCK_PROCESS_CPUTIME_ID;
		return 1;
	case LINUX_CLOCK_THREAD_CPUTIME_ID:
		*result = CLOCK_THREAD_CPUTIME_ID;
		return 1;
	}
	return 0;
}

static uint64_t clock_nanoseconds(clockid_t clock)
{
	struct timespec now;

	clock_gettime(clock, &now);
	return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

/* the calling thread's high-resolution timer (Windows 10 1803), closed as
the thread ends (its fiber-local slot's callback) */
static DWORD timer_slot = FLS_OUT_OF_INDEXES;
static INIT_ONCE timer_slot_once = INIT_ONCE_STATIC_INIT;

static void WINAPI timer_close(void *timer)
{
	if (timer)
		CloseHandle(timer);
}

static BOOL CALLBACK timer_slot_make(INIT_ONCE *once, void *parameter, void **context)
{
	(void)once;
	(void)parameter;
	(void)context;
	timer_slot = FlsAlloc(timer_close);
	return TRUE;
}

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

void host_sleep_nanoseconds(uint64_t nanoseconds)
{
	HANDLE timer = NULL;
	LARGE_INTEGER due;

	if (!nanoseconds)
	{
		Sleep(0);
		return;
	}
	InitOnceExecuteOnce(&timer_slot_once, timer_slot_make, NULL, NULL);
	if (timer_slot != FLS_OUT_OF_INDEXES)
	{
		timer = FlsGetValue(timer_slot);
		if (!timer)
		{
			timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
			if (timer)
				FlsSetValue(timer_slot, timer);
		}
	}
	/* (relative: negative, in 100 ns) */
	due.QuadPart = -(LONGLONG)((nanoseconds + 99) / 100);
	if (timer && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
	{
		WaitForSingleObject(timer, INFINITE);
		return;
	}
	Sleep((DWORD)((nanoseconds + 999999) / 1000000));
}

/* The high-resolution timer wakes a thread late, as the scheduler gets to it:
on a Windows 11 VM with an RTX 3090 (the frames' limit's 6 to 8 ms sleeps,
measured at a 128 Hz limit), 0.3 to 0.4 ms at the median, 0.5 to 0.65 ms at
the 99th percentile, never 0.8 ms; more on a busy computer. A frame limited
to 240 Hz, 4.2 ms, would come late by a tenth of its time, unevenly. So a
sleep until a time waits the timer until a margin before it and spins on the
performance counter (the monotonic clock's) for the rest. The margin is
the timer's own recent lateness: the 99th percentile of the last
WAKE_WINDOW waits' (the second latest of them), with WAKE_SLACK more, from
WAKE_FLOOR to WAKE_CEILING; WAKE_FIRST until WAKE_WINDOW waits have been
measured. A fixed millisecond spun 0.6 ms a frame, 8% of a core at 128 Hz.

No spin at all, the timer waited to the time itself: on battery (its spin is
power the frames' evenness is not worth), or where the frames are long
beside the margin (WAKE_SPIN_SHARE: a 30 Hz limit's 33 ms against half a
millisecond late). A wait shorter than the margin is spun whole. The deadline
stays absolute: each wait is from the clock read just before it. Only the
frames' limit sleeps this way (one thread, the guest's main one): every
other sleep (the game's Sleep(1), poll's steps) is host_sleep_nanoseconds's,
which spins for none of its time. HALO_FRAME_LIMIT_STATS=1 logs the
lateness, the margin and the spin each WAKE_REPORT frames. */
#define WAKE_WINDOW 128
#define WAKE_FIRST 1000000ULL
#define WAKE_SLACK 50000ULL
#define WAKE_FLOOR 100000ULL
#define WAKE_CEILING 2000000ULL
#define WAKE_SPIN_SHARE 50
#define WAKE_REPORT 1000

static struct
{
	uint64_t late[WAKE_WINDOW];
	unsigned count, next;
	uint64_t margin, previous_deadline;
	/* the power checked each second (GetSystemPowerStatus) */
	uint64_t power_checked;
	int on_battery;
	/* HALO_FRAME_LIMIT_STATS: -1 until read */
	int statistics;
	unsigned frames, spun_frames;
	uint64_t spun, late_most, late_total, missed_most;
} wake = { { 0 }, 0, 0, WAKE_FIRST, 0, 0, 0, -1 };

/* the margin from the recent waits' lateness: the second latest of the
window (its 99th percentile) */
static void wake_margin(void)
{
	uint64_t first = 0, second = 0, margin;
	unsigned index;

	for (index = 0; index < WAKE_WINDOW; index++)
	{
		uint64_t late = wake.late[index];

		if (late > first)
		{
			second = first;
			first = late;
		}
		else if (late > second)
			second = late;
	}
	margin = second + WAKE_SLACK;
	wake.margin = margin < WAKE_FLOOR ? WAKE_FLOOR : margin > WAKE_CEILING ? WAKE_CEILING : margin;
}

static void wake_report(void)
{
	if (wake.statistics < 0)
	{
		const char *value = getenv("HALO_FRAME_LIMIT_STATS");

		wake.statistics = value && *value && *value != '0';
	}
	if (!wake.statistics || ++wake.frames < WAKE_REPORT)
		return;
	host_logf(HOST_LOG_INFO, "frame limit: the timer late %.0f us on average, %.0f us at most; the margin %.0f us;"
		" %u of %u frames spun, %.0f us a frame; the deadline missed by %.0f us at most%s",
		(double)wake.late_total / wake.frames / 1000.0, (double)wake.late_most / 1000.0,
		(double)wake.margin / 1000.0, wake.spun_frames, wake.frames, (double)wake.spun / wake.frames / 1000.0,
		(double)wake.missed_most / 1000.0, wake.on_battery ? " (on battery)" : "");
	wake.frames = wake.spun_frames = 0;
	wake.spun = wake.late_most = wake.late_total = wake.missed_most = 0;
}

void host_sleep_until_nanoseconds(uint64_t deadline)
{
	uint64_t now = clock_nanoseconds(CLOCK_MONOTONIC), interval, margin, asked, woke, late;

	interval = wake.previous_deadline && deadline > wake.previous_deadline ? deadline - wake.previous_deadline : 0;
	wake.previous_deadline = deadline;
	if (now >= wake.power_checked + 1000000000ULL || now < wake.power_checked)
	{
		SYSTEM_POWER_STATUS power;

		wake.on_battery = GetSystemPowerStatus(&power) && power.ACLineStatus == 0;
		wake.power_checked = now;
	}
	margin = wake.on_battery || (interval && interval / WAKE_SPIN_SHARE > wake.margin) ? 0 : wake.margin;
	if (deadline > now + margin)
	{
		asked = deadline - margin;
		host_sleep_nanoseconds(asked - now);
		woke = clock_nanoseconds(CLOCK_MONOTONIC);
		late = woke > asked ? woke - asked : 0;
		wake.late[wake.next] = late;
		wake.next = (wake.next + 1) % WAKE_WINDOW;
		if (++wake.count >= WAKE_WINDOW)
		{
			wake.count = WAKE_WINDOW;
			if (!(wake.next % 16))
				wake_margin();
		}
		wake.late_total += late;
		wake.late_most = late > wake.late_most ? late : wake.late_most;
		now = woke;
	}
	if (now < deadline)
	{
		wake.spun += deadline - now;
		wake.spun_frames++;
		while (clock_nanoseconds(CLOCK_MONOTONIC) < deadline)
			YieldProcessor();
	}
	else if (now - deadline > wake.missed_most)
		wake.missed_most = now - deadline;
	wake_report();
}

/* ---------- futexes, on WaitOnAddress */

static long futex_wake(void *address, long long count)
{
	long long index;

	if (count <= 0)
		return 0;
	if (count >= 16)
	{
		/* (all of them; how many is not said) */
		WakeByAddressAll(address);
		return 1;
	}
	for (index = 0; index < count; index++)
		WakeByAddressSingle(address);
	return (long)count;
}

static long guest_futex(uint64_t guest, int operation, uint32_t value, uint64_t timeout, uint64_t guest2,
	uint32_t value3)
{
	volatile uint32_t *address = GUEST(volatile uint32_t *, guest);
	int command = operation & LINUX_FUTEX_CMD_MASK;

	if (!guest || (guest & 3))
		return -GUEST_EINVAL;
	switch (command)
	{
	case LINUX_FUTEX_WAIT:
	case LINUX_FUTEX_WAIT_BITSET:
	{
		DWORD milliseconds = INFINITE;
		int64_t seconds, nanoseconds;

		if (timespec_in(timeout, &seconds, &nanoseconds))
		{
			uint64_t wait = seconds < 0 ? 0 : (uint64_t)seconds * 1000000000ULL + (uint64_t)nanoseconds;

			if (command == LINUX_FUTEX_WAIT_BITSET)
			{
				/* (an absolute time, on the clock the flag names) */
				uint64_t now = clock_nanoseconds(operation & LINUX_FUTEX_CLOCK_REALTIME ? CLOCK_REALTIME :
					CLOCK_MONOTONIC);

				wait = wait > now ? wait - now : 0;
			}
			if (!wait)
				return *address == value ? -GUEST_ETIMEDOUT : -GUEST_EAGAIN;
			milliseconds = wait / 1000000 >= INFINITE ? INFINITE - 1 : (DWORD)((wait + 999999) / 1000000);
		}
		if (*address != value)
			return -GUEST_EAGAIN;
		if (!WaitOnAddress((volatile void *)address, &value, sizeof(value), milliseconds))
			return GetLastError() == ERROR_TIMEOUT ? -GUEST_ETIMEDOUT : -GUEST_EINTR;
		return 0;
	}
	case LINUX_FUTEX_WAKE:
	case LINUX_FUTEX_WAKE_BITSET:
		return futex_wake((void *)address, value);
	case LINUX_FUTEX_CMP_REQUEUE:
		if (*address != value3)
			return -GUEST_EAGAIN;
		/* fall through */
	case LINUX_FUTEX_REQUEUE:
		/* every waiter on the first address is woken: those the call would
		wake, and those it would move to the second address, which wait
		there again themselves (a spurious wakeup, which every futex user
		allows). musl's condition variables hand a broadcast on this way
		(unlock_requeue: wake 0, move 1), so waking only `value` would leave
		the moved waiters asleep for good (as port/macos/host_syscall.c) */
		(void)guest2;
		WakeByAddressAll((void *)address);
		return 1;
	}
	return -GUEST_ENOSYS;
}

/* ---------- polling (no descriptor polls but pipes and files, which are
always ready but a pipe with nothing to read) */

struct guest_pollfd
{
	int32_t fd;
	int16_t events;
	int16_t revents;
};

static long guest_ppoll(uint64_t fds_address, uint32_t count, uint64_t timeout)
{
	struct guest_pollfd *fds = GUEST(struct guest_pollfd *, fds_address);
	int64_t seconds = 0, nanoseconds = 0;
	int has_timeout = timespec_in(timeout, &seconds, &nanoseconds);
	uint64_t limit = has_timeout ? (seconds < 0 ? 0 : (uint64_t)seconds * 1000000000ULL + (uint64_t)nanoseconds) : 0;
	uint64_t start = clock_nanoseconds(CLOCK_MONOTONIC);
	uint32_t index;

	if (count && !fds)
		return -GUEST_EFAULT;
	for (;;)
	{
		long ready = 0;

		for (index = 0; index < count; index++)
		{
			HANDLE handle = handle_of(fds[index].fd);
			DWORD available = 0;

			fds[index].revents = 0;
			if (fds[index].fd < 0)
				continue;
			if (!valid_handle(handle))
				fds[index].revents = GUEST_POLLNVAL;
			else if (GetFileType(handle) == FILE_TYPE_PIPE && descriptor_writes_only(fds[index].fd))
			{
				/* (a pipe's write end, which PeekNamedPipe cannot read: a
				write goes ahead, or fails with EPIPE once the reader went) */
				fds[index].revents = GUEST_POLLOUT;
			}
			else if (GetFileType(handle) == FILE_TYPE_PIPE)
			{
				if (!PeekNamedPipe(handle, NULL, 0, NULL, &available, NULL))
					fds[index].revents = GUEST_POLLHUP;
				else
					fds[index].revents = (int16_t)((available ? GUEST_POLLIN : 0) | GUEST_POLLOUT);
			}
			else
				fds[index].revents = GUEST_POLLIN | GUEST_POLLOUT;
			fds[index].revents &= (int16_t)(fds[index].events | GUEST_POLLHUP | GUEST_POLLERR | GUEST_POLLNVAL);
			if (fds[index].revents)
				ready++;
		}
		if (ready)
			return ready;
		if (has_timeout && clock_nanoseconds(CLOCK_MONOTONIC) - start >= limit)
			return 0;
		if (!count)
		{
			/* (nothing to wait for: the time, or for ever, as Linux's,
			whose only way out would be a signal the guest never gets) */
			if (!has_timeout)
			{
				for (;;)
					Sleep(INFINITE);
			}
			host_sleep_nanoseconds(limit);
			return 0;
		}
		host_sleep_nanoseconds(1000000);
	}
}

/* ---------- the process */

static long guest_uname(uint64_t address)
{
	char *result = GUEST(char *, address);
	typedef LONG (WINAPI *version_function)(OSVERSIONINFOW *);
	version_function get_version =
		(version_function)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
	OSVERSIONINFOW version;
	DWORD size = 64;

	if (!address)
		return -GUEST_EFAULT;
	/* Linux's: six fields of 65 bytes */
	memset(result, 0, 6 * 65);
	strcpy(result + 0 * 65, "Windows");
	if (!GetComputerNameA(result + 1 * 65, &size))
		strcpy(result + 1 * 65, "localhost");
	memset(&version, 0, sizeof(version));
	version.dwOSVersionInfoSize = sizeof(version);
	if (get_version && get_version(&version) == 0)
	{
		snprintf(result + 2 * 65, 65, "%lu.%lu.%lu", (unsigned long)version.dwMajorVersion,
			(unsigned long)version.dwMinorVersion, (unsigned long)version.dwBuildNumber);
	}
	strcpy(result + 3 * 65, "#1");
	strcpy(result + 4 * 65, "x86_64");
	return 0;
}

static long guest_rlimit(int resource, uint64_t address)
{
	uint64_t *result = GUEST(uint64_t *, address);
	NT_TIB *tib = (NT_TIB *)NtCurrentTeb();

	if (!address)
		return 0;
	switch (resource)
	{
	case GUEST_RLIMIT_STACK:
		/* (this thread's stack, which the host made) */
		result[0] = result[1] = (uint64_t)((char *)tib->StackBase - *(char **)((char *)NtCurrentTeb() + 0x1478));
		return 0;
	case GUEST_RLIMIT_NOFILE:
		result[0] = result[1] = DESCRIPTORS_MAXIMUM;
		return 0;
	}
	result[0] = result[1] = ~0ULL;
	return 0;
}

/* the umask, which nothing on Windows reads */
static uint32_t file_mask = 022;

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
	case GUEST_SYS_read:
		return read_handle((int)a, GUEST(void *, b), (uint32_t)c, 0, 0);
	case GUEST_SYS_write:
		return write_handle((int)a, GUEST(const void *, b), (uint32_t)c, 0, 0);
	case GUEST_SYS_pread64:
		return read_handle((int)a, GUEST(void *, b), (uint32_t)c, (int64_t)d, 1);
	case GUEST_SYS_pwrite64:
		return write_handle((int)a, GUEST(const void *, b), (uint32_t)c, (int64_t)d, 1);
	case GUEST_SYS_readv:
		return guest_vector((int)a, (uint64_t)b, (int)c, 0, 0, 0);
	case GUEST_SYS_writev:
		return guest_vector((int)a, (uint64_t)b, (int)c, 0, 0, 1);
	case GUEST_SYS_preadv:
		return guest_vector((int)a, (uint64_t)b, (int)c, offset_of_halves(d, e), 1, 0);
	case GUEST_SYS_pwritev:
		return guest_vector((int)a, (uint64_t)b, (int)c, offset_of_halves(d, e), 1, 1);
	case GUEST_SYS_openat:
		return guest_openat((int)a, (uint64_t)b, (uint32_t)c, (uint32_t)d);
	case GUEST_SYS_close:
		return guest_close((int)a);
	case GUEST_SYS__llseek:
		return guest_llseek((int)a, (uint32_t)b, (uint32_t)c, (uint64_t)d, (int)e);
	case GUEST_SYS_fstat:
	{
		HANDLE handle = handle_of((int)a);

		return valid_handle(handle) ? stat_handle(handle, (uint64_t)b) : -GUEST_EBADF;
	}
	case GUEST_SYS_newfstatat:
		return guest_fstatat((int)a, (uint64_t)b, (uint64_t)c, (int)d);
	case GUEST_SYS_getdents64:
		return guest_getdents64((int)a, GUEST(unsigned char *, b), (uint32_t)c);
	case GUEST_SYS_unlinkat:
		return guest_unlinkat((int)a, (uint64_t)b, (int)c);
	case GUEST_SYS_renameat:
		return guest_renameat((int)a, (uint64_t)b, (int)c, (uint64_t)d, 0);
	case GUEST_SYS_renameat2:
		return guest_renameat((int)a, (uint64_t)b, (int)c, (uint64_t)d, (uint32_t)e);
	case GUEST_SYS_mkdirat:
		return guest_mkdirat((int)a, (uint64_t)b);
	case GUEST_SYS_fchmod:
		return guest_fchmod((int)a, (uint32_t)b);
	case GUEST_SYS_fchmodat:
		return guest_fchmodat((int)a, (uint64_t)b, (uint32_t)c);
	case GUEST_SYS_faccessat:
		return guest_faccessat((int)a, (uint64_t)b, (int)c);
	case GUEST_SYS_ftruncate:
		return guest_ftruncate((int)a, (int64_t)b);
	case GUEST_SYS_fsync:
	case GUEST_SYS_fdatasync:
		return guest_fsync((int)a);
	case GUEST_SYS_fcntl:
		return guest_fcntl((int)a, (int)b, (uint64_t)c);
	case GUEST_SYS_flock:
		return guest_flock((int)a, (int)b);
	case GUEST_SYS_getcwd:
		return guest_getcwd((uint64_t)a, (uint32_t)b);
	case GUEST_SYS_chdir:
		return guest_chdir((uint64_t)a);
	case GUEST_SYS_readlinkat:
		return guest_readlinkat((int)a, (uint64_t)b, (uint64_t)c, (uint32_t)d);
	case GUEST_SYS_utimensat:
		return guest_utimensat((int)a, (uint64_t)b, (uint64_t)c, (int)d);
	case GUEST_SYS_dup:
		return guest_dup((int)a, 0);
	case GUEST_SYS_dup3:
		return guest_dup3((int)a, (int)b, (uint32_t)c, 0);
	case GUEST_SYS_pipe2:
		return guest_pipe2((uint64_t)a, (uint32_t)b);
	case GUEST_SYS_ioctl:
		return -LINUX_ENOTTY;
	case GUEST_SYS_ppoll:
		return guest_ppoll((uint64_t)a, (uint32_t)b, (uint64_t)c);

	case GUEST_SYS_clock_gettime:
	case GUEST_SYS_clock_getres:
	{
		struct timespec value;
		clockid_t clock;

		if (!clock_of(a, &clock))
			return -GUEST_EINVAL;
		if ((number == GUEST_SYS_clock_gettime ? clock_gettime(clock, &value) : clock_getres(clock, &value)) != 0)
			return -GUEST_EINVAL;
		timespec_out((uint64_t)b, value.tv_sec, value.tv_nsec);
		return 0;
	}
	case GUEST_SYS_gettimeofday:
	{
		struct timespec value;

		clock_gettime(CLOCK_REALTIME, &value);
		timespec_out((uint64_t)a, value.tv_sec, value.tv_nsec / 1000);
		return 0;
	}
	case GUEST_SYS_nanosleep:
	{
		int64_t seconds, nanoseconds;

		if (!timespec_in((uint64_t)a, &seconds, &nanoseconds))
			return -GUEST_EFAULT;
		if (nanoseconds < 0 || nanoseconds >= 1000000000 || seconds < 0)
			return -GUEST_EINVAL;
		host_sleep_nanoseconds((uint64_t)seconds * 1000000000ULL + (uint64_t)nanoseconds);
		return 0;
	}
	case GUEST_SYS_clock_nanosleep:
	{
		int64_t seconds, nanoseconds;
		uint64_t wait;
		clockid_t clock;

		if (!timespec_in((uint64_t)c, &seconds, &nanoseconds))
			return -GUEST_EFAULT;
		if (!clock_of(a, &clock) || nanoseconds < 0 || nanoseconds >= 1000000000 || seconds < 0)
			return -GUEST_EINVAL;
		wait = (uint64_t)seconds * 1000000000ULL + (uint64_t)nanoseconds;
		if ((int)b & GUEST_TIMER_ABSTIME)
		{
			uint64_t now = clock_nanoseconds(clock);

			wait = wait > now ? wait - now : 0;
		}
		host_sleep_nanoseconds(wait);
		return 0;
	}
	case GUEST_SYS_futex:
		return guest_futex((uint64_t)a, (int)b, (uint32_t)c, (uint64_t)d, (uint64_t)e, (uint32_t)f);
	case GUEST_SYS_sched_yield:
		SwitchToThread();
		return 0;
	case GUEST_SYS_sched_getaffinity:
		/* (the host's processors, all of them: musl's processor count) */
		return halo_guest_affinity(GUEST(unsigned char *, c), (unsigned long)(uint32_t)b,
			(long)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
	/* the calling thread's priority (who 0, as Linux sets a thread's) */
	case GUEST_SYS_setpriority:
		/* (the lowest, 19: background mode, the thread's processor, disk and
		memory priority all lowered; world_sdf.c's bake) */
		if (a == 0 && b == 0 && (int)c >= 19 && SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN))
			return 0;
		if (a == 0 && b == 0 && (int)c > 0)
			SetThreadPriority(GetCurrentThread(), (int)c >= 10 ? THREAD_PRIORITY_LOWEST : THREAD_PRIORITY_BELOW_NORMAL);
		return 0;

	case GUEST_SYS_mmap:
		return host_guest_mmap((uint64_t)a, (uint64_t)b, (int)c, (int)d, (int)e, f);
	case GUEST_SYS_munmap:
		return host_guest_munmap((uint64_t)a, (uint64_t)b);
	case GUEST_SYS_mprotect:
		return host_guest_mprotect((uint64_t)a, (uint64_t)b, (int)c);
	case GUEST_SYS_madvise:
		return host_guest_madvise((uint64_t)a, (uint64_t)b, (int)c);
	case GUEST_SYS_mremap:
	case GUEST_SYS_brk:
		/* musl then falls back to mmap and copying */
		return -GUEST_ENOMEM;

	case GUEST_SYS_exit:
	case GUEST_SYS_exit_group:
		host_exit((int)a);

	case GUEST_SYS_set_tid_address:
	case GUEST_SYS_gettid:
		return (long)(GetCurrentThreadId() & 0x7fffffff);
	case GUEST_SYS_getpid:
		return (long)(GetCurrentProcessId() & 0x7fffffff);
	case GUEST_SYS_getppid:
		return 1;
	case GUEST_SYS_getuid:
	case GUEST_SYS_geteuid:
	case GUEST_SYS_getgid:
	case GUEST_SYS_getegid:
		return 1000;
	case GUEST_SYS_umask:
	{
		uint32_t previous = file_mask;

		file_mask = (uint32_t)a & 0777;
		return previous;
	}
	case GUEST_SYS_uname:
		return guest_uname((uint64_t)a);
	case GUEST_SYS_sysinfo:
		return guest_sysinfo((uint64_t)a);
	case GUEST_SYS_getrlimit:
		return guest_rlimit((int)a, (uint64_t)b);
	case GUEST_SYS_prlimit64:
		/* (only this process's, and only read) */
		if ((int)a != 0 && (int)a != (int)(GetCurrentProcessId() & 0x7fffffff))
			return -LINUX_ESRCH;
		return guest_rlimit((int)b, (uint64_t)d);
	case GUEST_SYS_getrandom:
		if (!b)
			return 0;
		if (BCryptGenRandom(NULL, GUEST(PUCHAR, a), (ULONG)(uint32_t)b, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
			return -LINUX_EIO;
		return (uint32_t)b;
	case GUEST_SYS_kill:
	case GUEST_SYS_tkill:
	case GUEST_SYS_tgkill:
	{
		long long signal_number = number == GUEST_SYS_tgkill ? c : b;

		/* (raise(), from abort(): the only signal the guest sends) */
		if (signal_number == GUEST_SIGABRT)
			host_abort("the guest aborted");
		return signal_number == 0 ? 0 : -GUEST_ENOSYS;
	}
	case GUEST_SYS_rt_sigprocmask:
		if (c)
			memset(GUEST(void *, c), 0, (size_t)(uint32_t)d);
		return 0;
	case GUEST_SYS_rt_sigaction:
	case GUEST_SYS_sigaltstack:
		/* the host owns exception handling */
		return 0;
	case GUEST_SYS_membarrier:
		/* (MEMBARRIER_CMD_QUERY, 0: none supported; any other, done) */
		if (a)
			FlushProcessWriteBuffers();
		return 0;

	default:
	{
		static unsigned char reported[512];

		if (number >= 0 && number < 512 && !reported[number])
		{
			reported[number] = 1;
			host_logf(HOST_LOG_WARN, "guest system call %lld is not supported", number);
		}
		return -GUEST_ENOSYS;
	}
	}
}
