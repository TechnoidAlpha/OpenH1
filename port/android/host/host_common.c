/*
HOST_COMMON.C

What every host's main shares (the Android app's, port/android/host, and
the Linux executable's, port/linux/host): logging and termination, the
guest's environment and boot block, and the debugging settings of
config.toml that the host reads itself.

The log's lines go to the sink the host's main chooses (host_log_set_sink).
Signal handlers log through it too (host_memory.c, host_debug.c), so a sink
writes with neither locks nor stdio. The last HOST_LOG_KEPT bytes of them
are kept as well, for the guest's Save Report (host_log_recent,
port/linux/src/report.c: host.log), since on Linux and Android they go only
to the standard error stream or logcat.

The crash record (host_crash_record, the guest's report.c): a file the
guest names, written at a crash (host_memory_watch.c's report, a fatal error,
an abort) with what the host logged of it and then the guest's log as
report.c keeps it in memory, oldest first, so that the next start can offer
to report it. It is written with open() and write() alone, which a signal
handler may call; on Windows with CreateFileW and WriteFile
(port/windows/host/host_crash_win.c), its path made whole as it is named,
and its lines written by the host's crash report (host_memory_watch_win.c).
*/

#include "host.h"
#include "host_render_record.h"
#include "host_render_stream.h"
#include "tomlc17.h"

#include <SDL3/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

/* ---------- logging and termination */

/* until the host sets one: the standard error stream, or on Android (whose
app has none) logcat */
static void default_sink(int priority, const char *line)
{
#ifdef __ANDROID__
	__android_log_write(priority, "halo", line);
#else
	size_t length = strlen(line);

	(void)priority;
	if (write(2, line, length) < 0 || write(2, "\n", 1) < 0)
		return;
#endif
}

static host_log_sink log_sink = default_sink;

/* the host's last lines, each ended by a newline (a power of 2) */
#define HOST_LOG_KEPT (64 * 1024)
/* the spins a line waits for the ring before it is let go (a signal
handler's line, on a thread that holds it, must not wait for ever) */
#define HOST_LOG_SPINS 100000

static struct
{
	char text[HOST_LOG_KEPT];
	unsigned long long written;
	volatile char busy;
} log_kept;

/* takes the ring with an atomic flag, not a lock: signal handlers log too */
static int log_kept_take(void)
{
	int spins;

	for (spins = 0; spins < HOST_LOG_SPINS; spins++)
		if (!__atomic_test_and_set(&log_kept.busy, __ATOMIC_ACQUIRE))
			return 1;
	return 0;
}

static void log_kept_give(void)
{
	__atomic_clear(&log_kept.busy, __ATOMIC_RELEASE);
}

static void log_keep(int priority, const char *line)
{
	const char *level = priority >= HOST_LOG_FATAL ? "fatal: " : priority >= HOST_LOG_ERROR ? "error: " :
		priority >= HOST_LOG_WARN ? "warning: " : "";
	const char *parts[3];
	int part;

	if (!log_kept_take())
		return;
	parts[0] = level;
	parts[1] = line;
	parts[2] = "\n";
	for (part = 0; part < 3; part++)
	{
		const char *character;

		for (character = parts[part]; *character; character++)
			log_kept.text[log_kept.written++ & (HOST_LOG_KEPT - 1)] = *character;
	}
	log_kept_give();
}

uint32_t host_log_recent(char *buffer, uint32_t size)
{
	unsigned long long oldest, start, index;
	uint32_t length = 0;

	if (!buffer || !size)
		return 0;
	if (!log_kept_take())
	{
		buffer[0] = 0;
		return 0;
	}
	oldest = log_kept.written > HOST_LOG_KEPT ? log_kept.written - HOST_LOG_KEPT : 0;
	start = oldest;
	if (log_kept.written - start > size - 1)
		start = log_kept.written - (size - 1);
	/* (from the start of a line: the one after the oldest kept, the first
	of which may be part of one, or after a newline the ring still has) */
	if (start && !(start > oldest && log_kept.text[(start - 1) & (HOST_LOG_KEPT - 1)] == '\n'))
	{
		while (start < log_kept.written && log_kept.text[start & (HOST_LOG_KEPT - 1)] != '\n')
			start++;
		if (start < log_kept.written)
			start++;
	}
	for (index = start; index < log_kept.written; index++)
		buffer[length++] = log_kept.text[index & (HOST_LOG_KEPT - 1)];
	log_kept_give();
	buffer[length] = 0;
	return length;
}

void host_log_set_sink(host_log_sink sink)
{
	log_sink = sink ? sink : default_sink;
}

void host_log(int priority, const char *text)
{
	log_keep(priority, text ? text : "");
	log_sink(priority, text ? text : "");
}

void host_logf(int priority, const char *format, ...)
{
	char line[1024];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	log_keep(priority, line);
	log_sink(priority, line);
}

/* ---------- the crash record */

static char crash_record_path[1024];
static const char *crash_record_log;
static uint32_t crash_record_log_size;
static const uint64_t *crash_record_log_written;
static volatile int crash_record_taken;
const char *volatile host_crash_cause;
#ifdef _WIN32
/* (host_crash_win.c's: the record's file by Windows's functions) */
void host_crash_file_name(const char *path);
void *host_crash_file_create(void);
void host_crash_file_write(void *file, const char *text, size_t length);
void host_crash_file_close(void *file);
static void *crash_record_handle;
#else
static int crash_record_file = -1;
#endif

void host_crash_record(const char *path, const char *log, uint32_t log_size, const uint64_t *log_written)
{
	snprintf(crash_record_path, sizeof(crash_record_path), "%s", path ? path : "");
	crash_record_log = log;
	crash_record_log_size = log_size;
	crash_record_log_written = log_written;
#ifdef _WIN32
	host_crash_file_name(crash_record_path);
#endif
}

static int crash_record_is_open(void)
{
#ifdef _WIN32
	return crash_record_handle != NULL;
#else
	return crash_record_file >= 0;
#endif
}

static void crash_record_write(const char *text, size_t length)
{
#ifdef _WIN32
	if (crash_record_handle)
		host_crash_file_write(crash_record_handle, text, length);
#else
	while (length && crash_record_file >= 0)
	{
		ssize_t written = write(crash_record_file, text, length);

		if (written <= 0)
			return;
		text += written;
		length -= (size_t)written;
	}
#endif
}

int host_crash_record_begin(const char *reason)
{
	/* (the first thread to crash writes it; the others' crashes are left out) */
	if (!crash_record_path[0] || __atomic_exchange_n(&crash_record_taken, 1, __ATOMIC_ACQ_REL))
		return 0;
#ifdef _WIN32
	crash_record_handle = host_crash_file_create();
#else
	crash_record_file = open(crash_record_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
#endif
	if (!crash_record_is_open())
		return 0;
	host_crash_record_line(reason);
	if (host_crash_cause)
	{
		crash_record_write("  ", 2);
		host_crash_record_line(host_crash_cause);
	}
	return 1;
}

void host_crash_record_line(const char *line)
{
	if (!crash_record_is_open() || !line)
		return;
	crash_record_write(line, strlen(line));
	crash_record_write("\n", 1);
}

void host_crash_record_end(void)
{
	static const char heading[] = "\n--- the log's last lines\n";
	uint64_t written, kept, first;

	if (!crash_record_is_open())
		return;
	if (crash_record_log && crash_record_log_size && crash_record_log_written)
	{
		written = *crash_record_log_written;
		kept = written < crash_record_log_size ? written : crash_record_log_size;
		first = (written - kept) % crash_record_log_size;
		crash_record_write(heading, sizeof(heading) - 1);
		/* (the ring's two parts: from the oldest byte kept to the end, then
		from the start) */
		if (first + kept > crash_record_log_size)
		{
			crash_record_write(crash_record_log + first, (size_t)(crash_record_log_size - first));
			crash_record_write(crash_record_log, (size_t)(first + kept - crash_record_log_size));
		}
		else
			crash_record_write(crash_record_log + first, (size_t)kept);
	}
#ifdef _WIN32
	host_crash_file_close(crash_record_handle);
	crash_record_handle = NULL;
#else
	close(crash_record_file);
	crash_record_file = -1;
#endif
}

void host_fatal(const char *format, ...)
{
	char message[1024];
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(message, sizeof(message), format, arguments);
	va_end(arguments);
	log_keep(HOST_LOG_FATAL, message);
	log_sink(HOST_LOG_FATAL, message);
	if (host_crash_record_begin("fatal error"))
	{
		host_crash_record_line(message);
		host_crash_record_end();
	}
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Halo", message, NULL);
	_exit(1);
}

/* (the record is the SIGABRT's, written by its handler with the stack:
host_memory_watch.c's, on Windows host_crash_win.c's; the reason a line of
it) */
void host_abort(const char *reason)
{
	host_logf(HOST_LOG_FATAL, "guest abort: %s", reason ? reason : "");
	host_crash_cause = reason;
	abort();
}

void host_exit(int code)
{
	host_logf(HOST_LOG_INFO, "the game exited (%d)", code);
	/* (a recording of the render stream ends with the records submitted:
	host_render_record.h) */
	if (host_render_record_active)
		host_render_stream_close();
	/* (the backend's pipeline cache written: port/macos/xgpu_metal.m) */
	host_render_stream_exiting();
	/* (what the host itself printed: the guest's output does not go
	through the host's stdio, host_syscall.c) */
	fflush(stdout);
	fflush(stderr);
	_exit(code);
}

#if defined(__APPLE__) || defined(_WIN32)
/* port/macos/host_syscall.c, port/windows/host/host_posix_win.c */
int host_linux_errno(int error);
#endif

int host_errno(void)
{
#if defined(__APPLE__) || defined(_WIN32)
	/* (the guest's errno values are Linux's) */
	return host_linux_errno(errno);
#else
	return errno;
#endif
}

/* ---------- the guest's environment */

static void environment_put(struct host_environment *environment, char *entry, size_t name_length)
{
	int index;

	for (index = 0; index < environment->count; index++)
	{
		if (!strncmp(environment->entries[index], entry, name_length) && environment->entries[index][name_length] == '=')
		{
			free(environment->entries[index]);
			environment->entries[index] = entry;
			return;
		}
	}
	if (environment->count < HOST_ENVIRONMENT_MAXIMUM)
	{
		environment->entries[environment->count++] = entry;
		return;
	}
	host_logf(HOST_LOG_WARN, "the guest's environment is full (%d entries): %.*s is left out",
		HOST_ENVIRONMENT_MAXIMUM, (int)name_length, entry);
	free(entry);
}

void host_environment_set(struct host_environment *environment, const char *name, const char *value)
{
	size_t name_length = strlen(name);
	size_t size = name_length + strlen(value) + 2;
	char *entry = malloc(size);

	if (!entry)
	{
		host_logf(HOST_LOG_ERROR, "out of memory for the guest's environment: %s is left out", name);
		return;
	}
	snprintf(entry, size, "%s=%s", name, value);
	environment_put(environment, entry, name_length);
}

void host_environment_add(struct host_environment *environment, const char *entry)
{
	const char *equals = strchr(entry, '=');
	char *copy;

	if (!equals || equals == entry)
		return;
	copy = strdup(entry);
	if (!copy)
	{
		host_logf(HOST_LOG_ERROR, "out of memory for the guest's environment: %.*s is left out",
			(int)(equals - entry), entry);
		return;
	}
	environment_put(environment, copy, (size_t)(equals - entry));
}

void host_time_zone(char *buffer, size_t size)
{
	time_t now = time(NULL);
	struct tm local;
	long offset;

	localtime_r(&now, &local);
#ifdef _WIN32
	/* (the 64-bit Windows host: its C runtime's struct tm has no
	tm_gmtoff) */
	offset = (long)(now - _mkgmtime(&local));
#else
	offset = -local.tm_gmtoff;
#endif
	snprintf(buffer, size, "<L>%s%ld:%02ld", offset < 0 ? "-" : "", labs(offset) / 3600, (labs(offset) / 60) % 60);
}

/* The boot block, then argv[2], then the environment's pointers (one more
for its NULL), then the strings. */
uint32_t host_make_boot(const struct host_environment *environment, const char *program)
{
	size_t pointers = sizeof(struct halo_guest_boot) + (2 + environment->count + 1) * sizeof(uint32_t);
	size_t size = pointers + strlen(program) + 1;
	struct halo_guest_boot *boot;
	uint32_t *argv, *environ_list;
	char *memory, *strings;
	int index;

	for (index = 0; index < environment->count; index++)
		size += strlen(environment->entries[index]) + 1;
	memory = host_low_map(size, PROT_READ | PROT_WRITE);
	if (!memory)
		host_fatal("cannot allocate the guest's environment (%zu bytes)", size);
	boot = (struct halo_guest_boot *)memory;
	argv = (uint32_t *)(memory + sizeof(*boot));
	environ_list = argv + 2;
	strings = memory + pointers;

	memcpy(strings, program, strlen(program) + 1);
	argv[0] = (uint32_t)(uintptr_t)strings;
	argv[1] = 0;
	strings += strlen(program) + 1;
	for (index = 0; index < environment->count; index++)
	{
		size_t length = strlen(environment->entries[index]) + 1;

		memcpy(strings, environment->entries[index], length);
		environ_list[index] = (uint32_t)(uintptr_t)strings;
		strings += length;
	}
	environ_list[index] = 0;
	boot->argc = 1;
	boot->argv = (uint32_t)(uintptr_t)argv;
	boot->environment = (uint32_t)(uintptr_t)environ_list;
	boot->page_size = (uint32_t)getpagesize();
	return (uint32_t)(uintptr_t)boot;
}

/* ---------- config.toml's settings the host reads */

#if defined(__linux__) && !defined(__ANDROID__)
#include <sys/resource.h>

/* (game.priority) the game's threads at the normal priority, not the
launcher's: a new thread takes its creator's niceness, and a launcher may
run at a low one (CachyOS's ananicy-cpp runs Steam at nice 16 as a
"Launcher", and the games Steam starts take it), which leaves the game's
threads behind every other program's work. Raised on the game thread before
it makes the others (the render thread, the workers), so they take it too;
the audio threads raise their own (host_sdl.c). Raising one's niceness
back needs RLIMIT_NICE or RealtimeKit, which SDL asks for when setpriority
is refused; a desktop has one or the other. "inherit": as launched */
static void host_apply_priority(const char *mode)
{
	int niceness;

	errno = 0;
	niceness = getpriority(PRIO_PROCESS, 0);
	if (errno)
		return;
	if (!strcmp(mode, "inherit"))
	{
		if (niceness > 0)
			host_logf(HOST_LOG_INFO, "priority: nice %d, the launcher's (game.priority \"inherit\")", niceness);
		return;
	}
	if (niceness <= 0)
		return;
	if (setpriority(PRIO_PROCESS, 0, 0) == 0 || SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_NORMAL))
	{
		errno = 0;
		host_logf(HOST_LOG_INFO, "priority: started at nice %d (the launcher's), now %d (game.priority)", niceness,
			getpriority(PRIO_PROCESS, 0));
	}
	else
	{
		host_logf(HOST_LOG_WARN, "priority: started at nice %d (the launcher's), and could not be raised (no "
			"RLIMIT_NICE, no RealtimeKit): the game runs behind the computer's other work", niceness);
	}
}
#endif

/* (display.astc_textures' default, as port_config.c's) */
char host_astc_textures[16] = "auto";
int host_pass_times;

void host_apply_debug_settings(const char *path, const char *hashes_reason)
{
	toml_result_t result = toml_parse_file_ex(path);
	double sample_seconds = 0.0;
	/* (debug.stall_ms' default, as port_config.c's) */
	double stall_ms = 100.0;
	int memory_watch = 1;
	/* (game.priority's default, as port_config.c's) */
	char priority_mode[16] = "normal";

	if (result.ok)
	{
		toml_datum_t seconds = toml_seek(result.toptab, "debug.sample_seconds");
		toml_datum_t watch = toml_seek(result.toptab, "debug.memory_watch");
		toml_datum_t astc = toml_seek(result.toptab, "display.astc_textures");
		toml_datum_t pass_times = toml_seek(result.toptab, "debug.pass_times");
		toml_datum_t stall = toml_seek(result.toptab, "debug.stall_ms");
		toml_datum_t priority = toml_seek(result.toptab, "game.priority");

		sample_seconds = seconds.type == TOML_FP64 ? seconds.u.fp64 :
			seconds.type == TOML_INT64 ? (double)seconds.u.int64 : 0.0;
		if (watch.type == TOML_BOOLEAN)
			memory_watch = watch.u.boolean ? 1 : 0;
		if (astc.type == TOML_STRING)
			snprintf(host_astc_textures, sizeof(host_astc_textures), "%s", astc.u.s);
		if (pass_times.type == TOML_INT64)
			host_pass_times = (int)pass_times.u.int64;
		if (stall.type == TOML_FP64 || stall.type == TOML_INT64)
			stall_ms = stall.type == TOML_FP64 ? stall.u.fp64 : (double)stall.u.int64;
		if (priority.type == TOML_STRING)
			snprintf(priority_mode, sizeof(priority_mode), "%s", priority.u.s);
		toml_free(result);
	}
	if (getenv("HALO_ASTC_TEXTURES") && *getenv("HALO_ASTC_TEXTURES"))
		snprintf(host_astc_textures, sizeof(host_astc_textures), "%s", getenv("HALO_ASTC_TEXTURES"));
	/* (the setting's variable, as the game's settings have one: the Linux
	host's environment) */
	if (getenv("HALO_PASS_TIMES") && *getenv("HALO_PASS_TIMES"))
		host_pass_times = atoi(getenv("HALO_PASS_TIMES"));
	if (getenv("HALO_SAMPLE"))
		sample_seconds = atof(getenv("HALO_SAMPLE"));
	if (getenv("HALO_PRIORITY") && *getenv("HALO_PRIORITY"))
		snprintf(priority_mode, sizeof(priority_mode), "%s", getenv("HALO_PRIORITY"));
#if defined(__linux__) && !defined(__ANDROID__)
	/* (before the threads the game makes: they take it) */
	host_apply_priority(priority_mode);
#else
	(void)priority_mode;
#endif
	if (getenv("HALO_STALL_MS") && *getenv("HALO_STALL_MS"))
		stall_ms = atof(getenv("HALO_STALL_MS"));
	host_stall_start(stall_ms);
	if (getenv("HALO_MEMORY_WATCH") && *getenv("HALO_MEMORY_WATCH"))
	{
		/* ("0", "false", "no" and "off" are false, as for the game's settings) */
		const char *value = getenv("HALO_MEMORY_WATCH");

		memory_watch = !(!strcmp(value, "0") || !strcasecmp(value, "false") || !strcasecmp(value, "no") ||
			!strcasecmp(value, "off"));
	}
	if (sample_seconds > 0.0)
	{
		char text[32];

		snprintf(text, sizeof(text), "%g", sample_seconds);
		host_debug_start_sampler(text);
	}
	if (hashes_reason)
	{
		host_memory_watch_use_hashes();
		host_logf(HOST_LOG_INFO, "write tracking: page hashes (%s)", hashes_reason);
	}
	else if (!memory_watch)
	{
		host_memory_watch_use_hashes();
		host_logf(HOST_LOG_INFO, "write tracking: page hashes (debug.memory_watch = false)");
	}
	else
	{
		host_logf(HOST_LOG_INFO, "write tracking: page protection");
	}
}
