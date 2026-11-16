/*
HOST_MEMORY_BASE.C

Guest address space with the base register (HALO_GUEST_BASE; host.h,
tools/guest_base_register.py), in place of host_memory.c: the guest's 4 GB
lie wherever the host could reserve them, at host_guest_base, a multiple of
4 GB. macOS needs this (its processes have no low 4 GB), and Linux and
Android may use it, which spares them host_memory.c's fixed low ranges.

The whole 4 GB is reserved at once, with guard regions below and above it
(an access a little before a null pointer, or past the end, faults as it
would in the low 4 GB). Inside it, the Xbox window and the image's range sit
at the guest addresses the guest was built for, and pages for everything
else (the guest's malloc arenas, thread stacks, anonymous mappings) are
handed out from the rest: first above the image (as host_memory.c does,
leaving the low addresses the Custom Edition tag cache wants), then from
16 MB up. The lowest 16 MB are never handed out.

The guest maps and protects its memory in 4 KB pages (its musl was built
for them, and port/linux/src/xbox_memory.c places the Xbox window's blocks
by them). Where the host's pages are larger (16 KB on macOS), each 4 KB
guest page is kept here with the protection the guest gave it: a host page
gets the union of its guest pages' protections, it is mapped while any of
them is, and a guest page mapped into a host page already in use is zeroed
in place.
*/

#include "host.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define GUEST_SIZE 0x100000000ULL
/* guard regions on both sides of the 4 GB */
#define GUARD_SIZE 0x01000000ULL
/* the low guest addresses no mapping is put at (null pointers) */
#define LOW_START 0x01000000ULL
/* the guest's pages */
#define GUEST_PAGE 0x1000ULL
#define GUEST_PAGE_COUNT (GUEST_SIZE / GUEST_PAGE)

/* Linux's mmap flags, which the guest's musl passes */
#define LINUX_MAP_SHARED 0x01
#define LINUX_MAP_PRIVATE 0x02
#define LINUX_MAP_FIXED 0x10
#define LINUX_MAP_ANONYMOUS 0x20
#define LINUX_MAP_NORESERVE 0x4000
#define LINUX_MAP_FIXED_NOREPLACE 0x100000

uintptr_t host_guest_base;

static size_t page_size;
static uint64_t page_count;
/* 1 for each host page handed out, or part of the window or image range */
static uint8_t *page_used;
static uint64_t next_upper, next_lower;
static pthread_mutex_t memory_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t reserve_once = PTHREAD_ONCE_INIT;
static int reserved;

/* each 4 KB guest page: 0 if unmapped, else GUEST_MAPPED and the
protection the guest gave it */
#define GUEST_MAPPED 0x100
static uint16_t *guest_pages;
static uint64_t image_base, image_end;

static uint64_t round_up(uint64_t value, uint64_t unit)
{
	return (value + unit - 1) & ~(unit - 1);
}

size_t host_page_size(void)
{
	if (!page_size)
		page_size = (size_t)sysconf(_SC_PAGESIZE);
	return page_size;
}

/* ---------- the reservation */

static void mark(uint64_t first, uint64_t count, uint8_t used)
{
	memset(page_used + first, used, count);
}

static void reserve(void)
{
	uint64_t size = 2 * GUEST_SIZE + 2 * GUARD_SIZE;
	char *mapping = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
	uintptr_t start, end, base;

	host_page_size();
	if (mapping == MAP_FAILED)
	{
		host_logf(HOST_LOG_ERROR, "cannot reserve the guest's 4 GB (%s)", strerror(errno));
		return;
	}
	start = (uintptr_t)mapping;
	end = start + size;
	base = (uintptr_t)round_up(start + GUARD_SIZE, GUEST_SIZE);
	/* (keep the guards; give back the rest) */
	if (base - GUARD_SIZE > start)
		munmap(mapping, base - GUARD_SIZE - start);
	if (base + GUEST_SIZE + GUARD_SIZE < end)
		munmap((void *)(base + GUEST_SIZE + GUARD_SIZE), end - (base + GUEST_SIZE + GUARD_SIZE));
	page_count = GUEST_SIZE / page_size;
	page_used = calloc(page_count, 1);
	guest_pages = calloc(GUEST_PAGE_COUNT, sizeof(*guest_pages));
	if (!page_used || !guest_pages)
		return;
	host_guest_base = base;
	/* the null pages, the window and the image's range are never handed out */
	mark(0, LOW_START / page_size, 1);
	mark(HALO_GUEST_WINDOW_BASE / page_size, HALO_GUEST_WINDOW_SIZE / page_size, 1);
	mark(HALO_GUEST_IMAGE_BASE / page_size, HALO_GUEST_IMAGE_RESERVE / page_size, 1);
	next_upper = (HALO_GUEST_IMAGE_BASE + HALO_GUEST_IMAGE_RESERVE) / page_size;
	next_lower = LOW_START / page_size;
	reserved = 1;
	host_logf(HOST_LOG_INFO, "guest memory at %016llx (pages of %zu bytes)", (unsigned long long)base, page_size);
}

static int ensure_reserved(void)
{
	pthread_once(&reserve_once, reserve);
	return reserved;
}

void host_memory_reserve_early(void)
{
	ensure_reserved();
}

int host_memory_fixed_unavailable(void)
{
	return !ensure_reserved();
}

void host_memory_report_low_mappings(FILE *file)
{
	if (file)
		fprintf(file, "the guest's memory is at %016llx\n", (unsigned long long)host_guest_base);
}

int host_memory_initialize(uint32_t base, uint32_t size)
{
	if (base != HALO_GUEST_IMAGE_BASE || size > HALO_GUEST_IMAGE_RESERVE)
	{
		host_logf(HOST_LOG_ERROR, "the guest image (%08x, %u bytes) does not fit its range", base, size);
		return -1;
	}
	if (!ensure_reserved())
	{
		errno = ENOMEM;
		return -1;
	}
	image_base = base;
	image_end = base + round_up(size, host_page_size());
	return 0;
}

/* ---------- pages */

/* the first of count free pages from page from up to page to, taken; 0
if none */
static uint64_t scan(uint64_t from, uint64_t to, uint64_t count)
{
	uint64_t page, run = 0;

	for (page = from; page < to; page++)
	{
		if (page_used[page])
		{
			run = 0;
			continue;
		}
		if (++run == count)
		{
			mark(page + 1 - count, count, 1);
			return page + 1 - count;
		}
	}
	return 0;
}

/* count free pages of the area from start to limit, looking from *next
on first (next fit); 0 if none */
static uint64_t take_from(uint64_t *next, uint64_t start, uint64_t limit, uint64_t count)
{
	uint64_t first = scan(*next, limit, count);

	if (!first)
		first = scan(start, *next + count < limit ? *next + count : limit, count);
	if (first)
		*next = first + count;
	return first;
}

static uint64_t take(uint64_t count)
{
	uint64_t first;

	pthread_mutex_lock(&memory_lock);
	first = take_from(&next_upper, (HALO_GUEST_IMAGE_BASE + HALO_GUEST_IMAGE_RESERVE) / page_size, page_count, count);
	if (!first)
		first = take_from(&next_lower, LOW_START / page_size, HALO_GUEST_WINDOW_BASE / page_size, count);
	pthread_mutex_unlock(&memory_lock);
	return first;
}

/* errno as the guest's number for it (Linux's) */
static int host_errno_value(void)
{
#ifdef __APPLE__
	extern int host_linux_errno(int error);

	return host_linux_errno(errno);
#else
	return errno;
#endif
}

static int in_range(uint64_t address, uint64_t size, uint64_t base, uint64_t end)
{
	return address >= base && address + size <= end && address + size >= address;
}

/* the union of the protections of the guest pages of the host page holding
guest address; -1 if none of them is mapped (memory_lock held) */
static int host_page_protection(uint64_t address)
{
	uint64_t first = (address & ~((uint64_t)page_size - 1)) / GUEST_PAGE;
	uint64_t page;
	int protection = -1;

	for (page = first; page < first + page_size / GUEST_PAGE; page++)
	{
		if (guest_pages[page] & GUEST_MAPPED)
			protection = (protection < 0 ? 0 : protection) | (guest_pages[page] & 0xff);
	}
	return protection;
}

/* (without memory_lock: the write tracking asks with its page lock held,
and update() takes that lock (host_memory_watch_forget) with memory_lock
held, so taking memory_lock here deadlocked the two. A read racing update()
does no harm: update() forgets the page, waiting for the page lock, before
it gives the host page its protection, so its protection is the last one
set) */
int host_memory_window_protection(uintptr_t address)
{
	uint64_t first = ((address - host_guest_base) & ~((uint64_t)page_size - 1)) / GUEST_PAGE;
	uint64_t page;
	int protection = -1;

	for (page = first; page < first + page_size / GUEST_PAGE; page++)
	{
		uint16_t entry = __atomic_load_n(&guest_pages[page], __ATOMIC_RELAXED);

		if (entry & GUEST_MAPPED)
			protection = (protection < 0 ? 0 : protection) | (entry & 0xff);
	}
	return protection < 0 ? PROT_NONE : protection;
}

void (*host_memory_remapped)(uint32_t address, uint32_t size);

int host_memory_readable_extent(uint32_t address, uint32_t size, uint32_t limit_start, uint32_t limit_end,
	uint32_t *start, uint32_t *end)
{
	uint64_t first = address & ~((uint64_t)page_size - 1);
	uint64_t last = round_up((uint64_t)address + (size ? size : 1), page_size);
	uint64_t lower = round_up(limit_start, page_size), upper = limit_end & ~((uint64_t)page_size - 1);
	uint64_t page;
	int readable = 1;

	if (!reserved || first < lower || last > upper)
		return 0;
	pthread_mutex_lock(&memory_lock);
	for (page = first; page < last && readable; page += page_size)
		readable = host_page_protection(page) >= 0 && (host_page_protection(page) & PROT_READ);
	if (readable)
	{
		while (first > lower && host_page_protection(first - page_size) >= 0 &&
			(host_page_protection(first - page_size) & PROT_READ))
			first -= page_size;
		while (last < upper && host_page_protection(last) >= 0 && (host_page_protection(last) & PROT_READ))
			last += page_size;
	}
	pthread_mutex_unlock(&memory_lock);
	*start = (uint32_t)first;
	*end = (uint32_t)last;
	return readable;
}

/* a host page of the window, the image's range or the null pages, which are
never handed out */
static int host_page_fixed(uint64_t address)
{
	return address < LOW_START ||
		in_range(address, 1, HALO_GUEST_WINDOW_BASE, HALO_GUEST_WINDOW_BASE + HALO_GUEST_WINDOW_SIZE) ||
		in_range(address, 1, HALO_GUEST_IMAGE_BASE, HALO_GUEST_IMAGE_BASE + HALO_GUEST_IMAGE_RESERVE);
}

/* Maps (protection >= 0: fresh zeroed pages; fd, offset: a file's), unmaps
(protection < 0) or (reprotect) changes the protection of the mapped guest
pages from address to address + size (guest addresses, 4 KB aligned), and
gives each host page they touch the union of its guest pages' protections.
A file is mapped only in whole host pages. memory_lock held. */
static int update(uint64_t address, uint64_t size, int protection, int reprotect, int flags, int fd, int64_t offset)
{
	uint64_t host_page;
	uint64_t first_host = address & ~((uint64_t)page_size - 1);
	uint64_t end_host = round_up(address + size, page_size);

	if (fd >= 0 && ((address | size) & (page_size - 1)))
		return -EINVAL;
	for (host_page = first_host; host_page < end_host; host_page += page_size)
	{
		int before = host_page_protection(host_page);
		uint64_t from = host_page > address ? host_page : address;
		uint64_t to = host_page + page_size < address + size ? host_page + page_size : address + size;
		void *host = (void *)host_address((uint32_t)host_page);
		uint64_t page;
		int after;

		for (page = from / GUEST_PAGE; page < to / GUEST_PAGE; page++)
		{
			if (reprotect)
			{
				if (guest_pages[page] & GUEST_MAPPED)
					guest_pages[page] = (uint16_t)(GUEST_MAPPED | protection);
			}
			else
			{
				guest_pages[page] = protection < 0 ? 0 : (uint16_t)(GUEST_MAPPED | protection);
			}
		}
		after = host_page_protection(host_page);
		/* (the write tracking's protection of a window page goes with it:
		what it watched there counts as written) */
		if (in_range(host_page, 1, HALO_GUEST_WINDOW_BASE, HALO_GUEST_WINDOW_BASE + HALO_GUEST_WINDOW_SIZE))
			host_memory_watch_forget((uint32_t)host_page, (uint32_t)page_size);
		if (after < 0)
		{
			/* nothing of it mapped: its memory goes back, and its address
			space too unless it is one of the fixed ranges */
			if (before >= 0)
			{
				mmap(host, page_size, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE | MAP_FIXED, -1, 0);
				if (host_memory_remapped)
					host_memory_remapped((uint32_t)host_page, (uint32_t)page_size);
			}
			if (!host_page_fixed(host_page))
				page_used[host_page / page_size] = 0;
		}
		else if (!reprotect && (before < 0 || fd >= 0 || (from == host_page && to == host_page + page_size)))
		{
			/* a whole host page mapped afresh */
			int host_flags = MAP_FIXED | (fd >= 0 ? flags : MAP_PRIVATE | MAP_ANON);

			if (mmap(host, page_size, after, host_flags, fd, fd >= 0 ? offset + (int64_t)(host_page - address) : 0) !=
				host)
				return -host_errno_value();
			page_used[host_page / page_size] = 1;
			if (host_memory_remapped)
				host_memory_remapped((uint32_t)host_page, (uint32_t)page_size);
		}
		else
		{
			if (!reprotect)
			{
				/* new guest pages in a host page already in use: zeroed in
				place */
				mprotect(host, page_size, PROT_READ | PROT_WRITE);
				memset((void *)host_address((uint32_t)from), 0, to - from);
			}
			mprotect(host, page_size, after);
		}
	}
	return 0;
}

void *host_low_map(size_t size, int protection)
{
	uint64_t count = round_up(size, host_page_size()) / page_size;
	uint64_t first;
	int result;

	if (!count || !ensure_reserved())
		return NULL;
	first = take(count);
	if (!first)
		return NULL;
	pthread_mutex_lock(&memory_lock);
	result = update(first * page_size, count * page_size, protection, 0, 0, -1, 0);
	pthread_mutex_unlock(&memory_lock);
	if (result != 0)
	{
		host_low_unmap((void *)host_address((uint32_t)(first * page_size)), count * page_size);
		return NULL;
	}
	return (void *)host_address((uint32_t)(first * page_size));
}

void host_low_unmap(void *address, size_t size)
{
	uint64_t start = ((uintptr_t)address - host_guest_base) & ~(GUEST_PAGE - 1);
	uint64_t end = round_up((uintptr_t)address - host_guest_base + size, GUEST_PAGE);

	if (!reserved || !host_in_guest((uintptr_t)address) || end > GUEST_SIZE)
		return;
	pthread_mutex_lock(&memory_lock);
	update(start, end - start, -1, 0, 0, -1, 0);
	pthread_mutex_unlock(&memory_lock);
}

int host_low_owns(uintptr_t address, size_t size)
{
	uint64_t guest = address - host_guest_base;
	uint64_t page;
	int result = 1;

	if (!reserved || !host_in_guest(address) || guest + size > GUEST_SIZE)
		return 0;
	if (in_range(guest, size, HALO_GUEST_WINDOW_BASE, HALO_GUEST_WINDOW_BASE + HALO_GUEST_WINDOW_SIZE) ||
		in_range(guest, size, image_base, image_end))
		return 1;
	pthread_mutex_lock(&memory_lock);
	for (page = guest / GUEST_PAGE; page <= (guest + (size ? size : 1) - 1) / GUEST_PAGE && result; page++)
		result = (guest_pages[page] & GUEST_MAPPED) != 0;
	pthread_mutex_unlock(&memory_lock);
	return result;
}

/* ---------- the guest's memory system calls (Linux's flags) */

static int host_map_flags(int flags)
{
	int result = 0;

	result |= flags & LINUX_MAP_SHARED ? MAP_SHARED : 0;
	result |= flags & LINUX_MAP_PRIVATE ? MAP_PRIVATE : 0;
	result |= flags & LINUX_MAP_ANONYMOUS ? MAP_ANON : 0;
	result |= flags & LINUX_MAP_NORESERVE ? MAP_NORESERVE : 0;
	return result;
}

/* whether the guest pages from address to address + size are unmapped
(memory_lock held) */
static int guest_range_free(uint64_t address, uint64_t size)
{
	uint64_t page;

	for (page = address / GUEST_PAGE; page < (address + size) / GUEST_PAGE; page++)
	{
		if (guest_pages[page] & GUEST_MAPPED)
			return 0;
	}
	return 1;
}

long host_guest_mmap(uint64_t address, uint64_t size, int protection, int flags, int fd, int64_t offset)
{
	uint64_t length = round_up(size, GUEST_PAGE);
	int anonymous = (flags & LINUX_MAP_ANONYMOUS) != 0;
	long result;

	if (!size || !ensure_reserved())
		return -EINVAL;
	address = (uint32_t)address;
	if (anonymous)
		fd = -1;
	if (flags & (LINUX_MAP_FIXED | LINUX_MAP_FIXED_NOREPLACE))
	{
		uint64_t host_page;

		if (address + length > GUEST_SIZE || (address & (GUEST_PAGE - 1)) || address < LOW_START)
			return -EINVAL;
		if (in_range(address, length, image_base, image_end))
			return -EINVAL;
		pthread_mutex_lock(&memory_lock);
		/* (inside the window, which is the guest's own, "no replace"
		replaces too) */
		if ((flags & LINUX_MAP_FIXED_NOREPLACE) &&
			!in_range(address, length, HALO_GUEST_WINDOW_BASE, HALO_GUEST_WINDOW_BASE + HALO_GUEST_WINDOW_SIZE) &&
			!guest_range_free(address, length))
		{
			pthread_mutex_unlock(&memory_lock);
			return -EEXIST;
		}
		/* (its host pages are taken, so that nothing else is put there) */
		for (host_page = address & ~((uint64_t)page_size - 1); host_page < address + length; host_page += page_size)
			page_used[host_page / page_size] = 1;
		result = update(address, length, protection, 0, host_map_flags(flags), fd, offset);
		pthread_mutex_unlock(&memory_lock);
		return result < 0 ? result : (long)address;
	}
	else
	{
		uint64_t count = round_up(length, host_page_size()) / page_size;
		uint64_t first = take(count);

		if (!first)
			return -ENOMEM;
		pthread_mutex_lock(&memory_lock);
		result = update(first * page_size, fd >= 0 ? count * page_size : length, protection, 0, host_map_flags(flags),
			fd, offset);
		if (result < 0)
			update(first * page_size, count * page_size, -1, 0, 0, -1, 0);
		pthread_mutex_unlock(&memory_lock);
		return result < 0 ? result : (long)(first * page_size);
	}
}

long host_guest_munmap(uint64_t address, uint64_t size)
{
	uint64_t length = round_up(size, GUEST_PAGE);

	address = (uint32_t)address;
	if (address + length > GUEST_SIZE || (address & (GUEST_PAGE - 1)))
		return -EINVAL;
	if (in_range(address, length, image_base, image_end))
		return -EINVAL;
	pthread_mutex_lock(&memory_lock);
	update(address, length, -1, 0, 0, -1, 0);
	pthread_mutex_unlock(&memory_lock);
	return 0;
}

long host_guest_mprotect(uint64_t address, uint64_t size, int protection)
{
	uint64_t length = round_up(size, GUEST_PAGE);
	long result;

	address = (uint32_t)address;
	if (address + length > GUEST_SIZE || (address & (GUEST_PAGE - 1)))
		return -EINVAL;
	pthread_mutex_lock(&memory_lock);
	if (in_range(address, length, image_base, image_end))
	{
		pthread_mutex_unlock(&memory_lock);
		return -EACCES;
	}
	result = update(address, length, protection, 1, 0, -1, 0);
	pthread_mutex_unlock(&memory_lock);
	return result;
}

/* the guest's madvise (host_syscall.c): the kernel's where the host's pages
are the guest's; with larger host pages (macOS's 16 KB) the kernel's would
reach the neighbouring guest pages, so only "don't need" changes anything,
and it clears the guest's pages, which then read as zero as the guest may
count on */
long host_guest_madvise(uint64_t address, uint64_t size, int advice)
{
	uint64_t length = round_up(size, GUEST_PAGE);
	long result = 0;

	address = (uint32_t)address;
	if (address + length > GUEST_SIZE || (address & (GUEST_PAGE - 1)))
		return -EINVAL;
	if (!length)
		return 0;
	if (in_range(address, length, image_base, image_end))
		return -EACCES;
	/* (host_low_owns takes the memory's lock itself) */
	if (!host_low_owns(host_address((uint32_t)address), length))
		return -ENOMEM;
	if (host_page_size() == GUEST_PAGE)
	{
		if (madvise((void *)host_address((uint32_t)address), length, advice) != 0)
			result = -host_errno_value();
	}
	else if (advice == MADV_DONTNEED)
	{
		memset((void *)host_address((uint32_t)address), 0, length);
	}
	if (!result)
		host_memory_watch_forget((uint32_t)address, (uint32_t)length);
	return result;
}
