/*
HOST_MAIN_WIN.C

Entry point of the 64-bit Windows build (tools/windows_build.py,
port/windows/README.md "64-bit Windows"): the game as the x32 guest image,
inside this executable (guest_image_win.S), in a 64-bit host.

At the top of main, before anything else is loaded (SDL3.dll is a
delay-loaded DLL; the drivers come with it), the guest's memory below 4 GB
is reserved (host_memory_win.c). If something already holds the fixed
ranges the guest was built for, the game starts again as a launcher would:
a copy of itself, suspended, whose fixed ranges this process reserves
before the copy runs, then waits for it and ends with its exit code.

The game then runs on the process's main thread, moved onto a stack in
guest memory (host_thread_win.c), as the guest's main thread: it makes the
window and pumps its events.

The guest is the desktop build of the game with the Windows code paths
(HALO_WINDOWS), so it finds the game data, the saves and config.toml as the
32-bit Windows build before it did (port/windows/README.md): it gets the part of
this process's environment it reads (its HALO_ settings, the user's
folders), in UTF-8, and /proc/self/exe is this executable. The log is the
standard error stream; without one (a release build has no console),
halo.log beside halo.exe.
*/

#include "host_win.h"

#include <SDL3/SDL.h>
#include <windows.h>
#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <timeapi.h>

/* guest_image_win.S */
extern const unsigned char halo_guest_image_start[];
extern const unsigned char halo_guest_image_end[];

/* laptops with a second, faster GPU (NVIDIA Optimus, AMD switchable
graphics) run a program on the integrated one unless it exports these */
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

/* the game's stack, as the Linux host's main thread's */
#define MAIN_STACK_SIZE (16 * 1024 * 1024)

/* ---------- the log: the standard error stream, or halo.log */

static HANDLE log_handle = INVALID_HANDLE_VALUE;
/* whether it is halo.log, which each line is added to the end of: the
launcher and the copy it starts write it both */
static int log_appends;

/* one write for each line, so that the lines of threads do not mix
(host_common.c: no stdio, as the exception handlers log through it too) */
static void log_sink(int priority, const char *line)
{
	const char *level = priority >= HOST_LOG_FATAL ? "fatal: " : priority >= HOST_LOG_ERROR ? "error: " :
		priority >= HOST_LOG_WARN ? "warning: " : "";
	char text[1200];
	int length = snprintf(text, sizeof(text), "halo-host: %s%s\n", level, line);
	DWORD written;

	if (length < 0)
		return;
	if (length >= (int)sizeof(text))
	{
		length = (int)sizeof(text) - 1;
		text[length - 1] = '\n';
	}
	if (log_handle != INVALID_HANDLE_VALUE)
	{
		OVERLAPPED at_end;

		/* (an offset of all ones: at the file's end, as one write) */
		memset(&at_end, 0, sizeof(at_end));
		at_end.Offset = at_end.OffsetHigh = 0xffffffff;
		WriteFile(log_handle, text, (DWORD)length, &written, log_appends ? &at_end : NULL);
	}
	if (IsDebuggerPresent())
		OutputDebugStringA(text);
}

/* the standard error stream, or (none: a release build) halo.log beside the
executable: made afresh, but in the copy the launcher started, which adds to
the launcher's lines; both write it (shared for writing) */
static void log_open(void)
{
	HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
	wchar_t path[MAX_PATH + 16];
	DWORD length;
	wchar_t *slash;

	if (handle && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN)
	{
		log_handle = handle;
		return;
	}
	length = GetModuleFileNameW(NULL, path, MAX_PATH);
	if (!length || length >= MAX_PATH || !(slash = wcsrchr(path, L'\\')))
		return;
	wcscpy(slash + 1, L"halo.log");
	log_handle = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		host_launched() ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	log_appends = log_handle != INVALID_HANDLE_VALUE;
}

/* ---------- the executable */

static char executable_path[MAX_PATH * 3];

const char *host_executable_path(void)
{
	return executable_path;
}

static void executable_path_find(void)
{
	wchar_t wide[MAX_PATH];
	DWORD length = GetModuleFileNameW(NULL, wide, MAX_PATH);
	char *at;

	if (!length || length >= MAX_PATH ||
		!WideCharToMultiByte(CP_UTF8, 0, wide, -1, executable_path, (int)sizeof(executable_path), NULL, NULL))
	{
		executable_path[0] = '\0';
		return;
	}
	/* (forward slashes: xbox_files.c finds the data folder by them) */
	for (at = executable_path; *at; at++)
	{
		if (*at == '\\')
			*at = '/';
	}
}

/* ---------- the system

The build needs Windows 10 or 11 (its timers, its manifest's UTF-8 code
page, Vulkan 1.3's drivers): an older Windows is told so plainly, at once,
rather than failing later in a way a player cannot read. Windows 10 before
version 1903 runs it, but takes no UTF-8 code page: its paths must then be
in the system's (the log says so). */

#define UTF8_CODE_PAGE_BUILD 18362

static int windows_supported(void)
{
	typedef LONG (WINAPI *version_function)(OSVERSIONINFOW *);
	version_function get_version =
		(version_function)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
	OSVERSIONINFOW version;

	memset(&version, 0, sizeof(version));
	version.dwOSVersionInfoSize = sizeof(version);
	if (!get_version || get_version(&version) != 0)
		return 1;
	host_logf(HOST_LOG_INFO, "Windows %lu.%lu (build %lu)", (unsigned long)version.dwMajorVersion,
		(unsigned long)version.dwMinorVersion, (unsigned long)version.dwBuildNumber);
	if (version.dwMajorVersion < 10)
	{
		MessageBoxW(NULL, L"This version of Halo needs 64-bit Windows 10 (version 1903 or later) or Windows 11, "
			L"and a graphics driver with Vulkan 1.3.\n\nThis computer has an older Windows.", L"Halo",
			MB_OK | MB_ICONERROR);
		return 0;
	}
	if (version.dwBuildNumber < UTF8_CODE_PAGE_BUILD)
	{
		host_logf(HOST_LOG_WARN, "Windows 10 before version 1903: the game's folders must have names in the "
			"system's code page (update Windows for any name)");
	}
	return 1;
}

/* Windows 11 throttles a process whose windows are minimised or hidden
behind others: EcoQoS (a slower, efficient core and clock) and, from
Windows 11, its timer resolution requests ignored, so that the 1 ms of
timeBeginPeriod (below) becomes 15.6 ms. The game keeps simulating and
sending (a network game, a dedicated server, a client alt-tabbed out), so
it opts out of both, at the cost of a little power in the background.
SetProcessInformation's ProcessPowerThrottling is Windows 10 1709's, its
IGNORE_TIMER_RESOLUTION Windows 11's: an older Windows refuses the flag
(asked again without it) or the call (said once, and the game runs on). */

#define POWER_THROTTLING_INFORMATION_CLASS 4 /* ProcessPowerThrottling */
#define POWER_THROTTLING_VERSION 1
#define POWER_THROTTLING_EXECUTION_SPEED 0x1
#define POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4

struct power_throttling_state
{
	ULONG version;
	ULONG control_mask;
	ULONG state_mask;
};

static void power_throttling_opt_out(void)
{
	typedef BOOL (WINAPI *set_information_function)(HANDLE, int, void *, DWORD);
	set_information_function set_information = (set_information_function)(void *)GetProcAddress(
		GetModuleHandleW(L"kernel32.dll"), "SetProcessInformation");
	struct power_throttling_state state;
	DWORD error;

	if (!set_information)
	{
		host_logf(HOST_LOG_INFO, "power throttling: not this Windows's (no SetProcessInformation)");
		return;
	}
	memset(&state, 0, sizeof(state));
	state.version = POWER_THROTTLING_VERSION;
	state.control_mask = POWER_THROTTLING_EXECUTION_SPEED | POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
	if (set_information(GetCurrentProcess(), POWER_THROTTLING_INFORMATION_CLASS, &state, sizeof(state)))
	{
		host_logf(HOST_LOG_INFO, "power throttling: off (EcoQoS and the timer resolution kept in the background)");
		return;
	}
	error = GetLastError();
	state.control_mask = POWER_THROTTLING_EXECUTION_SPEED;
	if (set_information(GetCurrentProcess(), POWER_THROTTLING_INFORMATION_CLASS, &state, sizeof(state)))
	{
		host_logf(HOST_LOG_INFO, "power throttling: EcoQoS off; the timer resolution's opt-out is not this "
			"Windows's (error %lu)", (unsigned long)error);
		return;
	}
	host_logf(HOST_LOG_INFO, "power throttling: left to Windows (error %lu)", (unsigned long)GetLastError());
}

/* ---------- the launcher */

/* this process's environment with HOST_LAUNCHED_VARIABLE set to this
process's id, for the copy alone (malloc'd); NULL on failure */
static wchar_t *launched_environment(void)
{
	wchar_t *block = GetEnvironmentStringsW(), *result;
	wchar_t entry[64];
	const wchar_t *at;
	size_t length, entry_length;

	if (!block)
		return NULL;
	for (at = block; *at; at += wcslen(at) + 1)
		;
	length = (size_t)(at - block);
	entry_length = (size_t)_snwprintf(entry, 64, L"%ls=%lu", HOST_LAUNCHED_VARIABLE,
		(unsigned long)GetCurrentProcessId()) + 1;
	result = malloc((length + entry_length + 1) * sizeof(wchar_t));
	if (result)
	{
		memcpy(result, block, length * sizeof(wchar_t));
		memcpy(result + length, entry, entry_length * sizeof(wchar_t));
		result[length + entry_length] = 0;
	}
	FreeEnvironmentStringsW(block);
	return result;
}

/* starts this executable again, suspended, with the fixed ranges reserved
in it before it runs; returns its exit code (never, if it cannot) */
static int launch_reserved(void)
{
	STARTUPINFOW startup;
	PROCESS_INFORMATION process;
	wchar_t path[MAX_PATH];
	wchar_t *command = _wcsdup(GetCommandLineW());
	/* (the variable in the copy's environment alone: this process's, which
	nothing else it starts inherits, stays without it) */
	wchar_t *environment = launched_environment();
	DWORD code = 1;

	if (!command || !environment || !GetModuleFileNameW(NULL, path, MAX_PATH))
		host_fatal("cannot start the game again");
	host_logf(HOST_LOG_WARN, "starting the game again with its memory reserved before it runs");
	memset(&startup, 0, sizeof(startup));
	startup.cb = sizeof(startup);
	if (!CreateProcessW(path, command, NULL, NULL, FALSE, CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, environment,
		NULL, &startup, &process))
	{
		host_fatal("cannot start the game again (error %lu)", (unsigned long)GetLastError());
	}
	free(command);
	free(environment);
	if (VirtualAllocEx(process.hProcess, (void *)(uintptr_t)HALO_GUEST_WINDOW_BASE, HALO_GUEST_WINDOW_SIZE,
		MEM_RESERVE, PAGE_NOACCESS) != (void *)(uintptr_t)HALO_GUEST_WINDOW_BASE ||
		VirtualAllocEx(process.hProcess, (void *)(uintptr_t)HALO_GUEST_IMAGE_BASE, HALO_GUEST_IMAGE_RESERVE,
		MEM_RESERVE, PAGE_NOACCESS) != (void *)(uintptr_t)HALO_GUEST_IMAGE_BASE)
	{
		TerminateProcess(process.hProcess, 1);
		host_fatal("The game cannot start: the memory it needs at 0x%08x and 0x%08x is taken by something "
			"loaded into it (an overlay or security program, maybe). halo.log lists what holds it.",
			HALO_GUEST_IMAGE_BASE, HALO_GUEST_WINDOW_BASE);
	}
	ResumeThread(process.hThread);
	CloseHandle(process.hThread);
	WaitForSingleObject(process.hProcess, INFINITE);
	GetExitCodeProcess(process.hProcess, &code);
	CloseHandle(process.hProcess);
	return (int)code;
}

/* ---------- the guest's environment */

/* what the game reads: its settings (HALO_...), where the saves go (the
roaming application data, port/linux/src/xbox_files.c) and the user's other
folders; the display and sound are the host's. TZ is game_main's. */
static int environment_passes(const char *entry)
{
	static const char *const prefixes[] =
	{
		"HALO_", "APPDATA=", "LOCALAPPDATA=", "USERPROFILE=", "HOME=", "USERNAME=", "TEMP=", "TMP=", "LANG=",
	};
	size_t index;

	for (index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]); index++)
	{
		if (!_strnicmp(entry, prefixes[index], strlen(prefixes[index])))
			return 1;
	}
	return 0;
}

static void environment_fill(struct host_environment *environment)
{
	wchar_t *block = GetEnvironmentStringsW();
	const wchar_t *entry;

	if (!block)
		return;
	for (entry = block; *entry; entry += wcslen(entry) + 1)
	{
		char *utf8 = host_utf8_from_wide(entry);

		if (utf8 && utf8[0] != '=' && environment_passes(utf8))
		{
			/* (the variables' names as the guest reads them: Windows keeps
			their case as they were set) */
			char *equals = strchr(utf8, '=');

			if (equals && !strncmp(utf8, "HALO_", 5))
				host_environment_add(environment, utf8);
			else if (equals)
			{
				size_t index;

				for (index = 0; utf8 + index < equals; index++)
					utf8[index] = (char)toupper((unsigned char)utf8[index]);
				host_environment_add(environment, utf8);
			}
		}
		free(utf8);
	}
	FreeEnvironmentStringsW(block);
}

/* ---------- main */

/* the game, on the main thread's stack in guest memory: the guest's
environment, the image, the debugging settings of config.toml (beside the
executable, where the game reads it), and the guest's main; never returns */
static void game_main(void *unused)
{
	struct host_environment environment = { { 0 }, 0 };
	char zone[64];
	char path[MAX_PATH * 3];
	const char *base;

	(void)unused;
	environment_fill(&environment);
	host_time_zone(zone, sizeof(zone));
	host_environment_set(&environment, "TZ", zone);
	if (host_load_image(halo_guest_image_start, (size_t)(halo_guest_image_end - halo_guest_image_start)) != 0)
		host_fatal("cannot load the game image; halo.log has the reason");
	host_register_guest_frames();
	base = SDL_GetBasePath();
	snprintf(path, sizeof(path), "%sconfig.toml", base ? base : "");
	host_apply_debug_settings(path, NULL);
	host_debug_thread_started();
	host_run_guest_main(host_make_boot(&environment, "halo"));
}

int main(void)
{
	int error;

	/* (a copy of halo.exe started to report a crash, or to send reports:
	no game) */
	if (host_crash_reporter())
		return 0;
	/* (read, and taken out of the environment, before anything starts
	another process) */
	host_launched();
	log_open();
	host_log_set_sink(log_sink);
	if (!windows_supported())
		return 1;
	executable_path_find();
	if (getenv("HALO_MEMORY_MAP") && *getenv("HALO_MEMORY_MAP") == '1')
	{
		host_logf(HOST_LOG_INFO, "the low 4 GB at start-up:");
		host_memory_log_low_map(NULL);
	}
	host_memory_reserve_early();
	if (!host_memory_fixed_reserved())
	{
		host_logf(HOST_LOG_ERROR, "the low 4 GB at start-up:");
		host_memory_log_low_map(NULL);
		if (!host_launched())
			return launch_reserved();
		host_fatal("The game cannot start: the memory it needs at 0x%08x and 0x%08x is taken. halo.log lists "
			"what holds it.", HALO_GUEST_IMAGE_BASE, HALO_GUEST_WINDOW_BASE);
	}
	/* (SDL3.dll, delay-loaded, which the host calls from here on: its
	absence is said plainly rather than as a crash) */
	if (!LoadLibraryExW(L"SDL3.dll", NULL, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))
	{
		host_logf(HOST_LOG_FATAL, "cannot load SDL3.dll (error %lu)", (unsigned long)GetLastError());
		MessageBoxW(NULL, L"SDL3.dll is missing. It belongs next to halo.exe.", L"Halo", MB_OK | MB_ICONERROR);
		return 1;
	}
	/* Sleep and the timers wait in 1 ms steps rather than 15.6 ms ones: the
	game's frame pacing sleeps for short intervals */
	timeBeginPeriod(1);
	power_throttling_opt_out();
	/* (the guest's text, and the log's, is UTF-8) */
	SetConsoleOutputCP(CP_UTF8);
	_setmode(0, _O_BINARY);
	_setmode(1, _O_BINARY);
	_setmode(2, _O_BINARY);
	host_install_signal_handlers();
	host_crash_install();
	error = host_run_on_low_stack(game_main, NULL, MAIN_STACK_SIZE);
	if (error)
		host_fatal("cannot allocate the game's stack in guest memory");
	host_fatal("the game returned");
}
