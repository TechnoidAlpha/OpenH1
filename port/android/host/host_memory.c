/*
HOST_MEMORY.C

Guest address space for the Android port.

Everything the guest touches must lie below 4 GB, but an Android process
shares that range with ART, whose heap spaces use compressed 32-bit
references and so also live there. The host therefore claims only what the
guest needs, when it needs it:

- the Xbox contiguous window at 0x80000000 and the image's own range, at
  start-up (both at fixed addresses the guest was built for);
- pools of address space for the guest's other mappings (malloc arenas,
  thread stacks), reserved in free gaps below 4 GB as they fill up.

The guest maps and protects its memory in 4 KB pages (PAGE: its musl's
PAGESIZE, and the Xbox window's blocks of port/linux/src/xbox_memory.c). The
kernel's pages (host_page_size) may be larger: 16 KB on Android 15 devices
that use them and on some ARM Linux kernels, 64 KB on others. Each guest page
is then kept here with the protection the guest gave it; a host page gets
the union of its guest pages' protections, it is mapped while any of them
is, and a guest page mapped into a host page already in use is zeroed in
place (host_memory_base.c keeps its pages so too). Everything handed out is
whole host pages.

The guest memory write tracking, which shares the fault handlers, is
host_memory_watch.c's. With the base register (HALO_GUEST_BASE) the guest's
memory is host_memory_base.c's instead of this file's.
*/

#include "host.h"
#include "host_watch_hash.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#else
/* (the Linux arm64 build's host, port/linux/host: no Android properties,
each read as unset) */
#define PROP_VALUE_MAX 92

static int __system_property_get(const char *name, char *value)
{
	(void)name;
	value[0] = '\0';
	return 0;
}
#endif
#include <ucontext.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

/* the guest's pages */
#define PAGE 0x1000ULL
#define LOW_LIMIT 0x100000000ULL
#define LOW_START 0x01000000ULL
#define POOL_SIZE (256ULL * 1024 * 1024)
/* (the most host pages a pool has: the host's are never smaller than the
guest's) */
#define POOL_PAGES (POOL_SIZE / PAGE)
#define MAXIMUM_POOLS 12

struct pool
{
	uint64_t base;
	/* host pages */
	uint32_t free_pages;
	/* 1 for each host page in use (the first POOL_SIZE / host_page_size()
	entries) */
	uint8_t used[POOL_PAGES];
};

/* A pool is never freed, and its base never changes: pool_count is
published after its entry (with release ordering), so host_low_owns reads
both without the lock, from signal handlers too. */
static struct pool *pools[MAXIMUM_POOLS];
static int pool_count;
static pthread_mutex_t memory_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t window_base, window_end;
static uint64_t image_base, image_end;

static uint64_t round_up(uint64_t value)
{
	return (value + PAGE - 1) & ~(PAGE - 1);
}

/* the host's pages (the kernel's), which every mapping and protection is
made in */
static uint64_t host_page;

size_t host_page_size(void)
{
	if (!host_page)
	{
		long size = sysconf(_SC_PAGESIZE);

		/* (a power of two, never smaller than the guest's) */
		host_page = size >= (long)PAGE && !(size & (size - 1)) ? (uint64_t)size : PAGE;
	}
	return (size_t)host_page;
}

static uint64_t host_round_up(uint64_t value)
{
	uint64_t unit = host_page_size();

	return (value + unit - 1) & ~(unit - 1);
}

static uint64_t host_round_down(uint64_t value)
{
	return value & ~((uint64_t)host_page_size() - 1);
}

static int in_range(uint64_t address, uint64_t size, uint64_t base, uint64_t end)
{
	return address >= base && address + size <= end && address + size >= address;
}

/* ---------- reading /proc/self/maps and smaps */

/* the next line of a /proc file into line, without its newline; a line
longer than the buffer (a long path) is cut short there, and the rest of it
skipped, so that it is never taken for lines of its own. 0 at the end. */
static int read_proc_line(FILE *file, char *line, size_t size)
{
	size_t length;

	if (!fgets(line, (int)size, file))
		return 0;
	length = strlen(line);
	if (length && line[length - 1] == '\n')
	{
		line[length - 1] = '\0';
		return 1;
	}
	for (;;)
	{
		int c = fgetc(file);

		if (c == '\n' || c == EOF)
			break;
	}
	return 1;
}

/* ---------- reserving address space below 4 GB */

/* the lowest free gap of at least size bytes at or above minimum, from
/proc/self/maps; 0 if none */
static uint64_t find_gap(uint64_t size, uint64_t minimum)
{
	FILE *maps = fopen("/proc/self/maps", "r");
	char line[512];
	uint64_t previous_end = minimum;
	uint64_t result = 0;

	if (!maps)
		return 0;
	while (read_proc_line(maps, line, sizeof(line)))
	{
		unsigned long long start, end;

		if (sscanf(line, "%llx-%llx", &start, &end) != 2)
			continue;
		if (end <= previous_end)
			continue;
		if (start > previous_end && start - previous_end >= size)
			break;
		previous_end = end > previous_end ? end : previous_end;
		if (previous_end >= LOW_LIMIT)
			break;
	}
	fclose(maps);
	previous_end = host_round_up(previous_end);
	if (previous_end + size <= LOW_LIMIT)
		result = previous_end;
	return result;
}

/* ART reserves its spaces at addresses chosen at zygote start, and on some
devices one of them covers the fixed Xbox memory window. ART's heap
(dalvik.vm.heapsize) decides how much of the low 4 GB its spaces take: on
handhelds with a large heap (the Retroid Pocket Flip2, the AYN Thor) the
free-list large object space usually lands right above the boot image, over
0x80000000.

That space commits pages lazily from its bottom, and the game's process
(HaloActivity runs in a process of its own, ":game", and reserves the
window from JNI_OnLoad before its Java side has done anything:
host_memory_reserve_early) hardly allocates large objects, so the slice
over the window is normally idle address space: unmap exactly the
intersection (never more) and let the caller retry. Only that space, and
only a slice with no page in use (present or swapped out): ART's other
spaces (its heap, bitmaps and card tables) are live, and unmapping them
would corrupt it where failing to start is at least clear. */
#define ART_LARGE_OBJECT_SPACE "[anon:dalvik-free list large object space]"

/* what range_usage found */
enum
{
	RANGE_UNUSED,
	RANGE_IN_USE,
	RANGE_UNKNOWN
};

/* why the fixed ranges could not be had: each reason also goes to the
log, and the report (host_memory_report_low_mappings) starts with them, so
memory_map.txt alone tells which case it was */
static char findings[2048];

static void finding(int priority, const char *format, ...)
{
	char line[512];
	size_t used = strlen(findings);
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	host_logf(priority, "%s", line);
	if (used + strlen(line) + 2 < sizeof(findings))
		snprintf(findings + used, sizeof(findings) - used, "%s\n", line);
}

struct range_usage
{
	uint64_t pages_in_use;
	uint64_t lowest_in_use, highest_in_use;
	const char *method;
};

static void note_in_use(struct range_usage *usage, uint64_t address)
{
	if (!usage->pages_in_use++)
		usage->lowest_in_use = address;
	usage->highest_in_use = address;
}

/* debug.halo.art_overlap's -nopagemap (simulate_art_overlap) */
static int simulated_no_pagemap;

/* from /proc/self/pagemap: bit 63 present, bit 62 swapped out (an entry
for each of the kernel's pages) */
static int range_usage_pagemap(uint64_t from, uint64_t to, struct range_usage *usage)
{
	int descriptor = simulated_no_pagemap ? -1 : open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
	uint64_t entries[512];
	uint64_t unit = host_page_size();
	uint64_t page = from / unit;

	if (simulated_no_pagemap)
		errno = EACCES;
	if (descriptor < 0)
	{
		finding(HOST_LOG_WARN, "cannot read /proc/self/pagemap (%s)", strerror(errno));
		return RANGE_UNKNOWN;
	}
	usage->method = "pagemap";
	while (page < to / unit)
	{
		size_t wanted = (size_t)(to / unit - page);
		ssize_t bytes;
		size_t count, index;

		if (wanted > sizeof(entries) / sizeof(entries[0]))
			wanted = sizeof(entries) / sizeof(entries[0]);
		bytes = pread(descriptor, entries, wanted * sizeof(entries[0]), (off_t)(page * sizeof(entries[0])));
		if (bytes <= 0 || bytes % sizeof(entries[0]))
		{
			finding(HOST_LOG_WARN, "cannot read /proc/self/pagemap at %08llx (%s)",
				(unsigned long long)(page * unit), bytes < 0 ? strerror(errno) : "short read");
			close(descriptor);
			return RANGE_UNKNOWN;
		}
		count = (size_t)bytes / sizeof(entries[0]);
		for (index = 0; index < count; index++)
		{
			if (entries[index] & (3ULL << 62))
				note_in_use(usage, (page + index) * unit);
		}
		page += count;
	}
	close(descriptor);
	return usage->pages_in_use ? RANGE_IN_USE : RANGE_UNUSED;
}

/* the Swap: line of the /proc/self/smaps entry starting at start, in kB; -1
if it cannot be read (or no entry starts there); with end, the entry must
also end there. dont_dump, if not NULL, tells whether its VmFlags have dd
(MADV_DONTDUMP). */
static long mapping_swap_kb_ending(uint64_t start, uint64_t end, int *dont_dump)
{
	FILE *smaps = fopen("/proc/self/smaps", "r");
	char line[512];
	int inside = 0;
	long result = -1;

	if (dont_dump)
		*dont_dump = 0;
	if (!smaps)
		return -1;
	while (read_proc_line(smaps, line, sizeof(line)))
	{
		unsigned long long lo, hi;
		long kb;

		if (sscanf(line, "%llx-%llx ", &lo, &hi) == 2 && strchr(line, '-') < strchr(line, ' '))
		{
			if (inside)
				break;
			inside = lo == start && (!end || hi == end);
			continue;
		}
		if (!inside)
			continue;
		if (sscanf(line, "Swap: %ld kB", &kb) == 1)
			result = kb;
		else if (dont_dump && !strncmp(line, "VmFlags:", 8))
			*dont_dump = strstr(line, " dd") != NULL;
	}
	fclose(smaps);
	return result;
}

static long mapping_swap_kb(uint64_t start)
{
	return mapping_swap_kb_ending(start, 0, NULL);
}

/* whether any of from..to (in the mapping starting at mapping_start) is
swapped out, when that mapping has swap somewhere: smaps counts swap per
mapping, so the range is made a mapping of its own for a moment
(MADV_DONTDUMP splits it off and changes nothing about its pages: neither
ART nor the game ever dumps core), read, and merged back (MADV_DODUMP). Not
for a mapping already marked so (the restore would then change it). -1 if
that cannot be done */
static long range_swap_kb(uint64_t mapping_start, uint64_t from, uint64_t to)
{
	int dont_dump;
	long kb;

	if (mapping_swap_kb_ending(mapping_start, 0, &dont_dump) < 0)
	{
		finding(HOST_LOG_WARN, "cannot read the smaps entry of ART's large object space at %08llx",
			(unsigned long long)mapping_start);
		return -1;
	}
	if (dont_dump)
	{
		finding(HOST_LOG_WARN, "ART's large object space at %08llx is marked not to be dumped; "
			"the swap of %08llx-%08llx cannot be read on its own", (unsigned long long)mapping_start,
			(unsigned long long)from, (unsigned long long)to);
		return -1;
	}
	if (madvise((void *)from, (size_t)(to - from), MADV_DONTDUMP) != 0)
	{
		finding(HOST_LOG_WARN, "cannot split %08llx-%08llx off ART's large object space (%s)",
			(unsigned long long)from, (unsigned long long)to, strerror(errno));
		return -1;
	}
	kb = mapping_swap_kb_ending(from, to, NULL);
	madvise((void *)from, (size_t)(to - from), MADV_DODUMP);
	if (kb < 0)
		finding(HOST_LOG_WARN, "no smaps entry for %08llx-%08llx once split off", (unsigned long long)from,
			(unsigned long long)to);
	return kb;
}

/* without pagemap (some kernels or policies refuse it): mincore() tells
which pages are resident (a byte for each of the kernel's), and smaps
whether any of the range is swapped out, which mincore cannot see */
static int range_usage_mincore(uint64_t mapping_start, uint64_t from, uint64_t to, struct range_usage *usage)
{
	unsigned char residency[4096];
	uint64_t address = from, unit = host_page_size();
	long swapped = mapping_swap_kb(mapping_start);

	usage->method = "mincore";
	if (swapped < 0)
	{
		finding(HOST_LOG_WARN, "cannot read the smaps entry of ART's large object space at %08llx",
			(unsigned long long)mapping_start);
		return RANGE_UNKNOWN;
	}
	if (swapped > 0)
	{
		/* (swap somewhere in the space: only the range's own matters) */
		long range_swapped = range_swap_kb(mapping_start, from, to);

		if (range_swapped < 0)
			return RANGE_UNKNOWN;
		if (range_swapped > 0)
		{
			/* (objects there, swapped out: which pages smaps does not say) */
			usage->method = "the range's swap in smaps";
			usage->pages_in_use = ((uint64_t)range_swapped * 1024 + unit - 1) / unit;
			usage->lowest_in_use = from;
			usage->highest_in_use = to - unit;
			return RANGE_IN_USE;
		}
		finding(HOST_LOG_INFO, "ART's large object space at %08llx: %ld kB of it swapped out, none of %08llx-%08llx",
			(unsigned long long)mapping_start, swapped, (unsigned long long)from, (unsigned long long)to);
	}
	while (address < to)
	{
		uint64_t length = to - address;
		size_t index;

		if (length > sizeof(residency) * unit)
			length = sizeof(residency) * unit;
		if (mincore((void *)address, length, residency) != 0)
		{
			finding(HOST_LOG_WARN, "mincore at %08llx failed (%s)", (unsigned long long)address, strerror(errno));
			return RANGE_UNKNOWN;
		}
		for (index = 0; index < (length + unit - 1) / unit; index++)
		{
			if (residency[index] & 1)
				note_in_use(usage, address + index * unit);
		}
		address += length;
	}
	return usage->pages_in_use ? RANGE_IN_USE : RANGE_UNUSED;
}

/* whether any page from..to (page aligned) of the mapping starting at
mapping_start holds data */
static int range_usage(uint64_t mapping_start, uint64_t from, uint64_t to, struct range_usage *usage)
{
	int result;

	memset(usage, 0, sizeof(*usage));
	result = range_usage_pagemap(from, to, usage);
	if (result == RANGE_UNKNOWN)
	{
		memset(usage, 0, sizeof(*usage));
		result = range_usage_mincore(mapping_start, from, to, usage);
	}
	return result;
}

static int reclaim_art_overlap(uint64_t address, uint64_t size)
{
	FILE *maps = fopen("/proc/self/maps", "r");
	char line[512];
	int reclaimed = 0;

	if (!maps)
		return 0;
	while (read_proc_line(maps, line, sizeof(line)))
	{
		unsigned long long lo, hi;
		uint64_t from, to;
		struct range_usage usage;
		char *name;
		int name_offset = 0;

		if (sscanf(line, "%llx-%llx %*s %*s %*s %*s %n", &lo, &hi, &name_offset) < 2 || !name_offset)
			continue;
		if (hi <= address || lo >= address + size)
			continue;
		name = line + name_offset;
		from = lo > address ? lo : address;
		to = hi < address + size ? hi : address + size;
		if (strcmp(name, ART_LARGE_OBJECT_SPACE))
		{
			finding(HOST_LOG_ERROR, "a fixed guest range is overlapped by %08llx-%08llx (%s), which is not ART's large object space; left alone",
				lo, hi, name[0] ? name : "unnamed");
			continue;
		}
		switch (range_usage(lo, from, to, &usage))
		{
		case RANGE_IN_USE:
			finding(HOST_LOG_ERROR,
				"ART's large object space %08llx-%08llx holds objects over %08llx-%08llx "
				"(%llu pages in use, %08llx-%08llx, by %s); left alone",
				lo, hi, (unsigned long long)from, (unsigned long long)to,
				(unsigned long long)usage.pages_in_use, (unsigned long long)usage.lowest_in_use,
				(unsigned long long)usage.highest_in_use + host_page_size(), usage.method);
			continue;
		case RANGE_UNKNOWN:
			finding(HOST_LOG_ERROR,
				"ART's large object space %08llx-%08llx covers %08llx-%08llx, and whether that part is in use "
				"cannot be told; left alone", lo, hi, (unsigned long long)from, (unsigned long long)to);
			continue;
		}
		if (munmap((void *)from, to - from) == 0)
		{
			host_logf(HOST_LOG_INFO,
				"reclaimed idle ART range %08llx-%08llx of %08llx-%08llx (%s, checked by %s)",
				(unsigned long long)from, (unsigned long long)to, lo, hi, name, usage.method);
			reclaimed = 1;
		}
	}
	fclose(maps);
	return reclaimed;
}

/* reclaim_art: the fixed ranges the guest was built for may take ART's
idle large object space (reclaim_art_overlap); the pools, placed in free
gaps, never do: a mapping in the way there is one ART just made, maybe live */
static int reserve(uint64_t address, uint64_t size, int reclaim_art)
{
	void *result = mmap((void *)address, size, PROT_NONE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);

	if (result == (void *)address)
		return 0;
	if (result != MAP_FAILED)
	{
		/* (a kernel older than 4.17 takes MAP_FIXED_NOREPLACE as a hint,
		and maps elsewhere when something is in the way) */
		munmap(result, size);
		errno = EEXIST;
	}
	if (reclaim_art && errno == EEXIST)
	{
		int error = errno;

		/* (the caller reports the first failure if nothing was reclaimed) */
		if (!reclaim_art_overlap(address, size))
		{
			errno = error;
			return -1;
		}
		result = mmap((void *)address, size, PROT_NONE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE,
			-1, 0);
		if (result == (void *)address)
			return 0;
		if (result != MAP_FAILED)
		{
			munmap(result, size);
			errno = EEXIST;
		}
	}
	return -1;
}

/* the mappings below 4 GB, for a report of why the fixed ranges could not
be had (logcat, and memory_map.txt in the data folder: host_main.c) */
void host_memory_report_low_mappings(FILE *file)
{
	FILE *maps = fopen("/proc/self/maps", "r");
	char line[512];
	char model[PROP_VALUE_MAX] = "", heap[PROP_VALUE_MAX] = "", release[PROP_VALUE_MAX] = "";

	__system_property_get("ro.product.model", model);
	__system_property_get("dalvik.vm.heapsize", heap);
	__system_property_get("ro.build.version.release", release);
	if (file)
	{
		fprintf(file, "device: %s, Android %s, Java heap %s\n", model[0] ? model : "unknown",
			release[0] ? release : "unknown", heap[0] ? heap : "unknown");
		fprintf(file, "why:\n%s\nmappings below 4 GB:\n", findings[0] ? findings : "(no reason recorded)\n");
	}
	if (!maps)
		return;
	while (read_proc_line(maps, line, sizeof(line)))
	{
		unsigned long long lo, hi;

		if (sscanf(line, "%llx-%llx", &lo, &hi) != 2 || lo >= LOW_LIMIT)
			continue;
		host_logf(HOST_LOG_INFO, "low mapping: %s", line);
		if (file)
			fprintf(file, "%s\n", line);
	}
	fclose(maps);
}

static struct pool *pool_new(void)
{
	/* above the image first: ART allocates its own low-4 GB memory from
	the bottom up */
	uint64_t minimum = image_end ? image_end : LOW_START;
	struct pool *pool;
	int attempt;

	if (pool_count == MAXIMUM_POOLS)
	{
		host_logf(HOST_LOG_ERROR, "guest memory: all %d pools are in use", MAXIMUM_POOLS);
		return NULL;
	}
	for (attempt = 0; attempt < 64; attempt++)
	{
		uint64_t address = find_gap(POOL_SIZE, minimum);

		if (!address && minimum != LOW_START)
		{
			minimum = LOW_START;
			address = find_gap(POOL_SIZE, minimum);
		}
		if (!address)
			return NULL;
		if (reserve(address, POOL_SIZE, 0) == 0)
		{
			pool = calloc(1, sizeof(*pool));
			if (!pool)
			{
				munmap((void *)(uintptr_t)address, POOL_SIZE);
				host_logf(HOST_LOG_ERROR, "guest memory: out of memory for a pool's page map");
				return NULL;
			}
			pool->base = address;
			pool->free_pages = (uint32_t)(POOL_SIZE / host_page_size());
			pools[pool_count] = pool;
			__atomic_store_n(&pool_count, pool_count + 1, __ATOMIC_RELEASE);
			host_logf(HOST_LOG_INFO, "guest memory pool %d at %08llx", pool_count - 1, (unsigned long long)address);
			return pool;
		}
		/* raced with another mapping; look further up */
		minimum = address + host_page_size();
	}
	return NULL;
}

/* The fixed ranges: the Xbox window (HALO_GUEST_WINDOW_SIZE: 512 MB) and
room for the guest image (HALO_GUEST_IMAGE_RESERVE: right above the window
on AArch64, below it on x86-64; halo_android_abi.h checks that they do not
overlap), reserved as early as the process allows
(host_memory_reserve_early, from JNI_OnLoad) or else when the image loads. */
static int fixed_reserved;
static int fixed_error;

/* when: the attempt, as the report names it (the reasons below it are
those of that attempt) */
static int reserve_fixed(const char *when)
{
	size_t used = strlen(findings);

	if (fixed_reserved)
		return 0;
	snprintf(findings + used, sizeof(findings) - used, "%s:\n", when);
	if (reserve(HALO_GUEST_WINDOW_BASE, HALO_GUEST_WINDOW_SIZE, 1) != 0)
	{
		fixed_error = errno;
		finding(HOST_LOG_ERROR, "cannot reserve the Xbox memory window at %08llx (%s)",
			(unsigned long long)HALO_GUEST_WINDOW_BASE, strerror(errno));
		return -1;
	}
	if (reserve(HALO_GUEST_IMAGE_BASE, HALO_GUEST_IMAGE_RESERVE, 1) != 0)
	{
		fixed_error = errno;
		finding(HOST_LOG_ERROR, "cannot reserve the guest image range at %08llx (%s)",
			(unsigned long long)HALO_GUEST_IMAGE_BASE, strerror(errno));
		munmap((void *)(uintptr_t)HALO_GUEST_WINDOW_BASE, HALO_GUEST_WINDOW_SIZE);
		return -1;
	}
	fixed_reserved = 1;
	return 0;
}

int host_memory_fixed_unavailable(void)
{
	return !fixed_reserved && fixed_error != 0;
}

/* For testing the reclaim on any device: with the system property
debug.halo.art_overlap set (adb shell setprop debug.halo.art_overlap idle,
or busy), a mapping named as ART's large object space is put over the fixed
ranges first, idle, or with a page in use inside the window, the way ART's
is on the devices that need the reclaim. Players never set it.

Either may be followed by -nopagemap, for devices that refuse
/proc/self/pagemap (the check then falls back to mincore and smaps), and
by -swapped, which pushes the stand-in's objects (its bottom pages, and the
window's page if busy) out to swap, as they can be on those devices. */
#ifndef MADV_PAGEOUT
#define MADV_PAGEOUT 21
#endif
#define PR_SET_VMA_ 0x53564d41
#define PR_SET_VMA_ANON_NAME_ 0
/* (over both fixed ranges, to past the image's) */
#define SIMULATED_SPACE_BASE 0x7c000000ULL
#define SIMULATED_SPACE_SIZE 0x2c000000ULL

static void simulate_art_overlap(void)
{
	char value[PROP_VALUE_MAX] = "";
	char *window_page = (char *)(uintptr_t)HALO_GUEST_WINDOW_BASE + 0x100000;
	int busy;
	void *space;

	if (__system_property_get("debug.halo.art_overlap", value) <= 0 || (strncmp(value, "idle", 4) && strncmp(value, "busy", 4)))
		return;
	busy = !strncmp(value, "busy", 4);
	space = mmap((void *)SIMULATED_SPACE_BASE, SIMULATED_SPACE_SIZE, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
	if (space != (void *)SIMULATED_SPACE_BASE)
	{
		host_logf(HOST_LOG_WARN, "debug.halo.art_overlap: %08llx-%08llx is not free",
			SIMULATED_SPACE_BASE, SIMULATED_SPACE_BASE + SIMULATED_SPACE_SIZE);
		if (space != MAP_FAILED)
			munmap(space, SIMULATED_SPACE_SIZE);
		return;
	}
	prctl(PR_SET_VMA_, PR_SET_VMA_ANON_NAME_, space, SIMULATED_SPACE_SIZE, "dalvik-free list large object space");
	/* objects at its bottom, as ART allocates them */
	memset(space, 0x5a, 4 * PAGE);
	if (busy)
		memset(window_page, 0x5a, PAGE);
	if (strstr(value, "-swapped"))
	{
		if (madvise(space, 4 * PAGE, MADV_PAGEOUT) != 0 || (busy && madvise(window_page, PAGE, MADV_PAGEOUT) != 0))
			host_logf(HOST_LOG_WARN, "debug.halo.art_overlap: cannot page out the stand-in (%s)", strerror(errno));
	}
	simulated_no_pagemap = strstr(value, "-nopagemap") != NULL;
	host_logf(HOST_LOG_WARN, "debug.halo.art_overlap=%s: a stand-in for ART's large object space at %08llx-%08llx "
		"(%ld kB of it swapped out)", value, SIMULATED_SPACE_BASE, SIMULATED_SPACE_BASE + SIMULATED_SPACE_SIZE,
		mapping_swap_kb(SIMULATED_SPACE_BASE));
}

void host_memory_reserve_early(void)
{
	simulate_art_overlap();
	if (reserve_fixed("at start-up (JNI_OnLoad)") == 0)
		host_logf(HOST_LOG_INFO, "reserved the Xbox memory window and the image range at start-up");
}

int host_memory_initialize(uint32_t base, uint32_t size)
{
	if (base != HALO_GUEST_IMAGE_BASE || size > HALO_GUEST_IMAGE_RESERVE)
	{
		host_logf(HOST_LOG_ERROR, "the guest image (%08x, %u bytes) does not fit its range", base, size);
		return -1;
	}
	if (reserve_fixed("when the image loaded") != 0)
	{
		errno = fixed_error;
		return -1;
	}
	window_base = HALO_GUEST_WINDOW_BASE;
	window_end = window_base + HALO_GUEST_WINDOW_SIZE;
	image_base = base;
	image_end = base + host_round_up(size);
	return 0;
}

/* ---------- the guest's pages

Each guest page below 4 GB that is the guest's (in the window, a pool or a
range it mapped itself; not the image's, which the loader maps): 0 if
unmapped, else GUEST_MAPPED and the protection the guest gave it. A host
page's protection is the union of its guest pages' (host_page_union), and
it is mapped while any of them is. Written under the memory lock; read
without it too (host_memory_window_protection, from the write tracking's
fault handler). */
#define GUEST_MAPPED 0x100
static uint16_t guest_pages[LOW_LIMIT / PAGE];

static void set_guest_page(uint64_t page, uint16_t value)
{
	__atomic_store_n(&guest_pages[page], value, __ATOMIC_RELAXED);
}

/* the union of the protections of the guest pages of the host page holding
address; -1 if none of them is mapped */
static int host_page_union(uint64_t address)
{
	uint64_t first = host_round_down(address) / PAGE, end = first + host_page_size() / PAGE, page;
	int protection = -1;

	for (page = first; page < end; page++)
	{
		uint16_t entry = __atomic_load_n(&guest_pages[page], __ATOMIC_RELAXED);

		if (entry & GUEST_MAPPED)
			protection = (protection < 0 ? 0 : protection) | (entry & 0xff);
	}
	return protection;
}

/* (host_memory_watch.c) the protection the guest gave the window's host
page at address: the union of its guest pages' (without the memory lock:
the write tracking asks from its fault handler, and update() forgets the
tracking's pages before it gives a host page its protection) */
int host_memory_window_protection(uintptr_t address)
{
	int protection = host_page_union(address);

	return protection < 0 ? PROT_NONE : protection;
}

/* ---------- page pools */

/* count free host pages of a pool, taken; NULL if none */
static void *pool_take(struct pool *pool, uint64_t count)
{
	uint64_t unit = host_page_size(), pages = POOL_SIZE / unit, run = 0, page;

	if (pool->free_pages < count)
		return NULL;
	for (page = 0; page < pages; page++)
	{
		if (pool->used[page])
		{
			run = 0;
			continue;
		}
		if (++run == count)
		{
			uint64_t first = page + 1 - count;

			memset(&pool->used[first], 1, count);
			pool->free_pages -= (uint32_t)count;
			return (void *)(uintptr_t)(pool->base + first * unit);
		}
	}
	return NULL;
}

/* count free host pages from any pool (a new one if need be), taken, with
the memory lock held; 0 if none */
static uint64_t pools_take(uint64_t count)
{
	void *address = NULL;
	int index;

	if (!count || count > POOL_SIZE / host_page_size())
		return 0;
	for (index = 0; index < pool_count && !address; index++)
		address = pool_take(pools[index], count);
	if (!address)
	{
		struct pool *pool = pool_new();

		if (pool)
			address = pool_take(pool, count);
	}
	return (uint64_t)(uintptr_t)address;
}

static struct pool *pool_of(uint64_t address, uint64_t size)
{
	int count = __atomic_load_n(&pool_count, __ATOMIC_ACQUIRE);
	int index;

	for (index = 0; index < count; index++)
	{
		if (in_range(address, size, pools[index]->base, pools[index]->base + POOL_SIZE))
			return pools[index];
	}
	return NULL;
}

/* ---------- what a host page below 4 GB is */

enum
{
	PAGE_HOST, /* the host's, or free address space */
	PAGE_POOL,
	PAGE_WINDOW,
	PAGE_IMAGE,
	PAGE_GUEST, /* a range the guest mapped itself */
};

/* (address: a host page's) */
static int page_kind(uint64_t address, struct pool **pool)
{
	uint64_t unit = host_page_size();

	*pool = NULL;
	if (in_range(address, unit, window_base, window_end))
		return PAGE_WINDOW;
	if (in_range(address, unit, image_base, image_end))
		return PAGE_IMAGE;
	*pool = pool_of(address, unit);
	if (*pool)
		return PAGE_POOL;
	return host_page_union(address) >= 0 ? PAGE_GUEST : PAGE_HOST;
}

/* the end of the run of host pages from address (before end) of the same
kind, and pool, as the first */
static uint64_t run_end(uint64_t address, uint64_t end, int *kind, struct pool **pool)
{
	uint64_t unit = host_page_size();
	struct pool *next_pool;

	*kind = page_kind(address, pool);
	for (address += unit; address < end; address += unit)
	{
		if (page_kind(address, &next_pool) != *kind || next_pool != *pool)
			break;
	}
	return address;
}

/* whether every host page of a range is the guest's (PAGE_IMAGE too, unless
image is 0), with the memory lock held */
static int guest_owns(uint64_t address, uint64_t end, int image)
{
	address = host_round_down(address);
	end = host_round_up(end);
	while (address < end)
	{
		struct pool *pool;
		int kind;

		address = run_end(address, end, &kind, &pool);
		if (kind == PAGE_HOST || (kind == PAGE_IMAGE && !image))
			return 0;
	}
	return 1;
}

/* whether a range touches the image's */
static int touches_image(uint64_t address, uint64_t end)
{
	return image_end && address < image_end && end > image_base;
}

/* the write tracking forgets the window's pages that are remapped or
reprotected */
static void forget_window_range(uint64_t address, uint64_t length)
{
	uint64_t start = address, end = address + length;

	if (start < window_base)
		start = window_base;
	if (end > window_end)
		end = window_end;
	if (start < end)
		host_memory_watch_forget((uint32_t)start, (uint32_t)(end - start));
}

/* ---------- the system calls on host pages, a run of alike ones at a time */

enum
{
	RUN_NONE,
	RUN_MAP, /* mapped afresh: zeroed, or a file's */
	RUN_PROTECT,
	RUN_RESERVE, /* back to reserved address space (a pool's, the window's) */
	RUN_UNMAP, /* back to the system (a range of the guest's own) */
};

struct run
{
	int action, protection, flags, fd;
	uint64_t start, end;
	int64_t offset;
	/* 0, or -errno of the first call that failed */
	long status;
};

static void run_flush(struct run *run)
{
	void *start = (void *)(uintptr_t)run->start;
	size_t length = (size_t)(run->end - run->start);
	int failed = 0;

	switch (run->action)
	{
	case RUN_MAP:
		failed = mmap(start, length, run->protection, run->flags | MAP_FIXED, run->fd, run->offset) != start;
		break;
	case RUN_PROTECT:
		failed = mprotect(start, length, run->protection) != 0;
		break;
	case RUN_RESERVE:
		failed = mmap(start, length, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) !=
			start;
		break;
	case RUN_UNMAP:
		failed = munmap(start, length) != 0;
		break;
	}
	if (failed && !run->status)
		run->status = -errno;
	run->action = RUN_NONE;
}

/* one more host page for the run, which is made first if the page cannot
join it */
static void run_add(struct run *run, int action, uint64_t start, int protection, int flags, int fd, int64_t offset)
{
	uint64_t end = start + host_page_size();

	if (run->action == action && run->end == start && run->protection == protection && run->flags == flags &&
		run->fd == fd && (fd < 0 || run->offset + (int64_t)(start - run->start) == offset))
	{
		run->end = end;
		return;
	}
	run_flush(run);
	run->action = action;
	run->start = start;
	run->end = end;
	run->protection = protection;
	run->flags = flags;
	run->fd = fd;
	run->offset = offset;
}

/* ---------- the guest's pages changed */

enum
{
	UPDATE_MAP, /* fresh zeroed pages, or fd's from offset */
	UPDATE_UNMAP,
	UPDATE_PROTECT, /* the mapped ones */
};

/* Maps, unmaps or reprotects the guest pages from address to address +
length (PAGE aligned; each host page they touch the window's, a pool's or a
range of the guest's own, never the image's), and gives each host page they
touch the union of its guest pages' protections: mapped afresh where all of
its guest pages are mapped now (or none was), zeroed in place where pages
are mapped into it while others are in use, and given back (to its pool,
the window's reservation, or the system) once none of its guest pages is
mapped. A file is mapped in whole host pages (guest_mmap_fixed checks).
The write tracking forgets the window's host pages first, as their
protection is set again. With the memory lock held; 0, or -errno. */
static long update(uint64_t address, uint64_t length, int operation, int protection, int flags, int fd,
	int64_t offset)
{
	uint64_t unit = host_page_size(), end = address + length, first = host_round_down(address), host;
	struct run run = { RUN_NONE, 0, 0, -1, 0, 0, 0, 0 };

	forget_window_range(first, host_round_up(end) - first);
	for (host = first; host < end; host += unit)
	{
		uint64_t from = host > address ? host : address, to = host + unit < end ? host + unit : end, page;
		struct pool *pool;
		/* (what it is, before its guest pages change) */
		int kind = page_kind(host, &pool);
		int before = host_page_union(host), after;
		uint8_t *used = pool ? &pool->used[(host - pool->base) / unit] : NULL;

		for (page = from / PAGE; page < to / PAGE; page++)
		{
			if (operation == UPDATE_MAP)
				set_guest_page(page, (uint16_t)(GUEST_MAPPED | (protection & 0xff)));
			else if (operation == UPDATE_UNMAP)
				set_guest_page(page, 0);
			else if (guest_pages[page] & GUEST_MAPPED)
				set_guest_page(page, (uint16_t)(GUEST_MAPPED | (protection & 0xff)));
		}
		after = host_page_union(host);
		if (after < 0)
		{
			/* none of its guest pages mapped: given back */
			if (kind == PAGE_POOL && (before >= 0 || *used))
				run_add(&run, RUN_RESERVE, host, PROT_NONE, 0, -1, 0);
			else if (kind == PAGE_WINDOW && before >= 0)
				run_add(&run, RUN_RESERVE, host, PROT_NONE, 0, -1, 0);
			else if (kind == PAGE_GUEST)
				run_add(&run, RUN_UNMAP, host, PROT_NONE, 0, -1, 0);
			if (used && *used)
			{
				*used = 0;
				pool->free_pages++;
			}
			continue;
		}
		if (used && !*used)
		{
			*used = 1;
			pool->free_pages--;
		}
		if (operation == UPDATE_MAP && (before < 0 || fd >= 0 || (from == host && to == host + unit)))
		{
			/* a host page mapped afresh */
			run_add(&run, RUN_MAP, host, after, flags, fd, fd >= 0 ? offset + (int64_t)(host - address) : 0);
			continue;
		}
		if (operation == UPDATE_MAP)
		{
			/* guest pages mapped into a host page in use: zeroed in place */
			run_flush(&run);
			if (mprotect((void *)(uintptr_t)host, (size_t)unit, PROT_READ | PROT_WRITE) != 0)
			{
				if (!run.status)
					run.status = -errno;
				continue;
			}
			memset((void *)(uintptr_t)from, 0, (size_t)(to - from));
		}
		run_add(&run, RUN_PROTECT, host, after, 0, -1, 0);
	}
	run_flush(&run);
	return run.status;
}

/* the guest pages of a range of the guest's (the memory lock held) that are
mapped */
static int any_mapped(uint64_t address, uint64_t end)
{
	uint64_t page;

	for (page = address / PAGE; page < end / PAGE; page++)
	{
		if (guest_pages[page] & GUEST_MAPPED)
			return 1;
	}
	return 0;
}

/* ---------- the host's own allocations */

void *host_low_map(size_t size, int protection)
{
	uint64_t length = host_round_up(size), address;

	if (!length || length < size)
		return NULL;
	pthread_mutex_lock(&memory_lock);
	address = pools_take(length / host_page_size());
	if (address && update(address, length, UPDATE_MAP, protection, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) != 0)
	{
		update(address, length, UPDATE_UNMAP, 0, 0, -1, 0);
		address = 0;
	}
	pthread_mutex_unlock(&memory_lock);
	return (void *)(uintptr_t)address;
}

/* what host_low_map gave goes back to its pool: its host pages, their
memory with them (the address space stays reserved) */
void host_low_unmap(void *address, size_t size)
{
	uint64_t start = host_round_down((uint64_t)(uintptr_t)address);
	uint64_t end = host_round_up((uint64_t)(uintptr_t)address + size);

	pthread_mutex_lock(&memory_lock);
	if (end > start && pool_of(start, end - start))
		update(start, end - start, UPDATE_UNMAP, 0, 0, -1, 0);
	pthread_mutex_unlock(&memory_lock);
}

int host_low_owns(uintptr_t address, size_t size)
{
	if (in_range(address, size, window_base, window_end) || in_range(address, size, image_base, image_end))
		return 1;
	return pool_of(address, size) != NULL;
}

/* ---------- the guest's memory system calls

The guest maps, unmaps, reprotects and advises only memory that is its own:
the pools' pages, the Xbox window, the image's range, and the ranges it
mapped itself at addresses of its choosing (MAP_FIXED_NOREPLACE in free
address space). The rest of the low 4 GB is the host's (on Android, ART's
too). Which guest pages are mapped is kept (guest_pages), so that a "no
replace" mapping fails where something is mapped, as the kernel's does. All
under the memory lock. */

static long guest_mmap_fixed(uint64_t address, uint64_t length, int protection, int flags, int fd, int64_t offset)
{
	uint64_t unit = host_page_size(), end = address + length, at;
	uint64_t first = host_round_down(address), last = host_round_up(end);
	int replace = !(flags & MAP_FIXED_NOREPLACE);
	int host = 0, owned = 0, image = 0;
	void *result;
	long status;

	if (address & (PAGE - 1))
		return -EINVAL;
	if (end > LOW_LIMIT || end < address)
		return -ENOMEM;
	/* (a file only in whole host pages: its pages are the kernel's) */
	if (fd >= 0 && ((address | (uint64_t)offset) & (unit - 1)))
		return -EINVAL;
	pthread_mutex_lock(&memory_lock);
	for (at = first; at < last;)
	{
		struct pool *pool;
		int kind;
		uint64_t next = run_end(at, last, &kind, &pool);

		if (kind == PAGE_HOST)
			host = 1;
		else
			owned = 1;
		if (kind == PAGE_IMAGE)
			image = 1;
		at = next;
	}
	if (!owned)
	{
		/* free address space, or the host's: a range of the guest's own
		where the kernel finds nothing mapped, in whole host pages */
		if (replace)
		{
			status = -EINVAL;
			goto done;
		}
		result = mmap((void *)(uintptr_t)first, (size_t)(last - first), protection, flags, fd, offset);
		if (result == MAP_FAILED)
			status = -errno;
		else if (result != (void *)(uintptr_t)first)
		{
			/* (a kernel older than 4.17 takes MAP_FIXED_NOREPLACE as a
			hint) */
			munmap(result, (size_t)(last - first));
			status = -EEXIST;
		}
		else
		{
			for (at = address / PAGE; at < end / PAGE; at++)
				set_guest_page(at, (uint16_t)(GUEST_MAPPED | (protection & 0xff)));
			status = (long)address;
		}
		goto done;
	}
	if (host)
	{
		/* (partly the host's: never replaced, and not free either) */
		status = replace ? -EINVAL : -EEXIST;
		goto done;
	}
	if (image)
	{
		/* (the image's is the loader's) */
		status = replace ? -EINVAL : -EEXIST;
		goto done;
	}
	if (!replace && any_mapped(address, end))
	{
		status = -EEXIST;
		goto done;
	}
	/* (a file's last host page: not into one whose other guest pages are in
	use) */
	if (fd >= 0 && last > end && any_mapped(end, last))
	{
		status = -EINVAL;
		goto done;
	}
	/* the guest's own memory: a reservation (or, with MAP_FIXED, a mapping)
	is replaced */
	status = update(address, length, UPDATE_MAP, protection, flags & ~MAP_FIXED_NOREPLACE, fd, offset);
	if (status == 0)
		status = (long)address;
done:
	pthread_mutex_unlock(&memory_lock);
	return status;
}

long host_guest_mmap(uint64_t address, uint64_t size, int protection, int flags, int fd, int64_t offset)
{
	uint64_t length = round_up(size), unit = host_page_size(), host;
	long status;

	if (!length || length < size)
		return -EINVAL;
	if (flags & MAP_ANONYMOUS)
		fd = -1;
	if (flags & (MAP_FIXED | MAP_FIXED_NOREPLACE))
		return guest_mmap_fixed(address, length, protection, flags, fd, offset);
	if (fd >= 0 && ((uint64_t)offset & (unit - 1)))
		return -EINVAL;
	pthread_mutex_lock(&memory_lock);
	host = pools_take(host_round_up(length) / unit);
	if (!host)
	{
		pthread_mutex_unlock(&memory_lock);
		return -ENOMEM;
	}
	/* (only the guest's pages mapped: the rest of the last host page is
	free for its next mapping, and goes back with it) */
	status = update(host, length, UPDATE_MAP, protection, flags, fd, offset);
	if (status != 0)
		update(host, host_round_up(length), UPDATE_UNMAP, 0, 0, -1, 0);
	pthread_mutex_unlock(&memory_lock);
	return status != 0 ? status : (long)host;
}

/* the guest pages go: a pool's host pages back to their pool, the
window's to its reservation, and the guest's own ranges to the system, as
none of their guest pages is mapped any longer; nothing is done unless all
of the range is one of these (the image is never unmapped) */
long host_guest_munmap(uint64_t address, uint64_t size)
{
	uint64_t length = round_up(size), end = address + length;
	long status = 0;

	if ((address & (PAGE - 1)) || !length || length < size)
		return -EINVAL;
	if (end > LOW_LIMIT || end < address)
		return -EINVAL;
	pthread_mutex_lock(&memory_lock);
	if (!guest_owns(address, end, 0))
		status = -EINVAL;
	else
		update(address, length, UPDATE_UNMAP, 0, 0, -1, 0);
	pthread_mutex_unlock(&memory_lock);
	return status;
}

/* mprotect and madvise only on memory that is the guest's (above); on the
image's only in whole host pages, as the kernel's */
static long guest_range(uint64_t address, uint64_t size, uint64_t *length)
{
	uint64_t end;
	int owned;

	if (address & (PAGE - 1))
		return -EINVAL;
	*length = round_up(size);
	if (*length < size)
		return -ENOMEM;
	if (!*length)
		return 0;
	end = address + *length;
	if (end > LOW_LIMIT || end < address)
		return -ENOMEM;
	if (touches_image(address, end) && ((address | end) & (host_page_size() - 1)))
		return -EINVAL;
	pthread_mutex_lock(&memory_lock);
	owned = guest_owns(address, end, 1);
	pthread_mutex_unlock(&memory_lock);
	return owned ? 1 : -ENOMEM;
}

long host_guest_mprotect(uint64_t address, uint64_t size, int protection)
{
	uint64_t length;
	long status = guest_range(address, size, &length);

	if (status <= 0)
		return status;
	if (touches_image(address, address + length))
	{
		/* (the image's pages are the loader's, kept by none of the above) */
		return mprotect((void *)(uintptr_t)address, length, protection) != 0 ? -errno : 0;
	}
	pthread_mutex_lock(&memory_lock);
	status = update(address, length, UPDATE_PROTECT, protection, 0, -1, 0);
	pthread_mutex_unlock(&memory_lock);
	return status;
}

/* The kernel's madvise where the guest's pages are whole host pages. Of a
host page partly in the range, only "don't need" changes anything: it
zeroes the range's mapped guest pages, which then read as zero as the guest
may count on (the kernel's would reach the neighbouring guest pages). The
write tracking forgets the window's host pages, whose contents may change
underneath it, and their protection is set again. */
long host_guest_madvise(uint64_t address, uint64_t size, int advice)
{
	uint64_t length, unit = host_page_size(), end, host, first, last;
	struct run run = { RUN_NONE, 0, 0, -1, 0, 0, 0, 0 };
	long status = guest_range(address, size, &length);

	if (status <= 0)
		return status;
	end = address + length;
	first = (address + unit - 1) & ~(unit - 1);
	last = end & ~(unit - 1);
	if (touches_image(address, end))
		return madvise((void *)(uintptr_t)address, length, advice) != 0 ? -errno : 0;
	pthread_mutex_lock(&memory_lock);
	forget_window_range(host_round_down(address), host_round_up(end) - host_round_down(address));
	if (first < last && madvise((void *)(uintptr_t)first, (size_t)(last - first), advice) != 0)
		status = -errno;
	else
		status = 0;
	for (host = host_round_down(address); host < end && status == 0; host += unit)
	{
		uint64_t from = host > address ? host : address, to = host + unit < end ? host + unit : end, page;
		int protection = host_page_union(host);

		if (protection < 0)
			continue;
		if ((from != host || to != host + unit) && advice == MADV_DONTNEED)
		{
			if (mprotect((void *)(uintptr_t)host, (size_t)unit, PROT_READ | PROT_WRITE) != 0)
			{
				status = -errno;
				break;
			}
			for (page = from / PAGE; page < to / PAGE; page++)
			{
				if (guest_pages[page] & GUEST_MAPPED)
					memset((void *)(uintptr_t)(page * PAGE), 0, PAGE);
			}
			run_add(&run, RUN_PROTECT, host, protection, 0, -1, 0);
		}
		else if (in_range(host, unit, window_base, window_end))
		{
			/* (the protection the tracking may have given it, which it
			has forgotten) */
			run_add(&run, RUN_PROTECT, host, protection, 0, -1, 0);
		}
	}
	run_flush(&run);
	if (!status)
		status = run.status;
	pthread_mutex_unlock(&memory_lock);
	return status;
}
