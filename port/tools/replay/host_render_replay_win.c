/*
HOST_RENDER_REPLAY_WIN.C

What the replays' shared part (host_render_replay.c) does with Windows's
functions in the 64-bit Windows build's replay (port/linux/vulkan/tools/
halo_replay.c, tools/windows_build.py): the recording mapped, and the
guest's memory in the low 4 GB, as the game's host has it
(port/windows/host/host_memory_win.c). A 64-bit Windows process has nearly
all of its low 4 GB free, so the granules (64 KB) of the memory the
recording names are reserved and committed before the driver loads, and any
other free granule there that the stream writes (a synchronous record's
answer) is committed as it is first touched, by a vectored exception
handler. Apart from host_render_replay.c, whose names <windows.h>'s macros
would take (its TEXT).
*/

#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>

#define GRANULE 0x10000ull
#define LOW_START 0x10000ull
#define LOW_LIMIT 0x100000000ull

/* the granules of the low 4 GB this process holds for the guest */
static volatile uint8_t claimed[LOW_LIMIT / GRANULE];

/* the recording's file mapped whole (read only), its size in *size; NULL
if it cannot be */
const unsigned char *host_render_replay_map(const char *path, size_t *size)
{
	wchar_t wide[4096];
	HANDLE file, mapping;
	LARGE_INTEGER length;
	const unsigned char *view;

	if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, (int)(sizeof(wide) / sizeof(wide[0]))))
		return NULL;
	file = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file == INVALID_HANDLE_VALUE)
		return NULL;
	if (!GetFileSizeEx(file, &length) || !length.QuadPart)
	{
		CloseHandle(file);
		return NULL;
	}
	mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
	CloseHandle(file);
	if (!mapping)
		return NULL;
	view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
	CloseHandle(mapping);
	if (!view)
		return NULL;
	*size = (size_t)length.QuadPart;
	return view;
}

/* a free granule claimed (reserved and committed, read and write); nonzero
if it is the guest's */
static int granule_claim(uint64_t granule)
{
	void *at = (void *)(uintptr_t)(granule * GRANULE);

	if (claimed[granule])
		return 1;
	if (VirtualAlloc(at, GRANULE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) != at)
		return 0;
	claimed[granule] = 1;
	return 1;
}

/* the guest's range [address, address + size) claimed where it is not
yet; nonzero if all of it is the guest's */
int host_render_replay_low_claim(uint32_t address, uint32_t size)
{
	uint64_t granule, last;

	if (!size || address < LOW_START)
		return 0;
	last = ((uint64_t)address + size - 1) / GRANULE;
	for (granule = address / GRANULE; granule <= last && granule < LOW_LIMIT / GRANULE; granule++)
	{
		if (!granule_claim(granule))
		{
			fprintf(stderr, "halo_replay: the guest's memory at %08llx is not free\n",
				(unsigned long long)(granule * GRANULE));
			return 0;
		}
	}
	return 1;
}

/* (any thread) a touch of a free granule of the low 4 GB claims it */
static LONG CALLBACK low_fault(EXCEPTION_POINTERS *exception)
{
	const EXCEPTION_RECORD *record = exception->ExceptionRecord;
	MEMORY_BASIC_INFORMATION region;
	uint64_t address;

	if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2)
		return EXCEPTION_CONTINUE_SEARCH;
	address = (uint64_t)record->ExceptionInformation[1];
	if (address < LOW_START || address >= LOW_LIMIT || claimed[address / GRANULE])
		return EXCEPTION_CONTINUE_SEARCH;
	if (!VirtualQuery((void *)(uintptr_t)(address & ~(GRANULE - 1)), &region, sizeof(region)) ||
		region.State != MEM_FREE || !granule_claim(address / GRANULE))
		return EXCEPTION_CONTINUE_SEARCH;
	return EXCEPTION_CONTINUE_EXECUTION;
}

void host_render_replay_low_watch(void)
{
	AddVectoredExceptionHandler(1, low_fault);
}

/* standard output's replies as they are written (no "\r\n" for "\n") */
void host_render_replay_binary_output(void)
{
	_setmode(_fileno(stdout), _O_BINARY);
}

/* (the POSIX sleeps' of port/windows/host/host_posix_win.c, which the
game's host has in host_syscall_win.c with the guest's system calls) */
void host_sleep_nanoseconds(uint64_t nanoseconds)
{
	LARGE_INTEGER frequency, start, now;

	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&start);
	/* (the scheduler's sleep to within a millisecond or two, then a spin) */
	if (nanoseconds >= 3000000ull)
		Sleep((DWORD)(nanoseconds / 1000000ull) - 2);
	do
	{
		YieldProcessor();
		QueryPerformanceCounter(&now);
	} while ((uint64_t)(now.QuadPart - start.QuadPart) * 1000000000ull / (uint64_t)frequency.QuadPart < nanoseconds);
}
