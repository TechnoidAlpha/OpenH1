/*
HOST_PAGES_SIM.C

The simulated kernel of host_pages_sim.h, on the build machine's own (whose
pages are 4 KB): its calls made on the real kernel with their addresses
checked against, and their lengths rounded up to, the simulated pages.
Compiled without host_pages_sim.h.
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

unsigned long sim_page_size = 0x4000;
unsigned long sim_reported_page_size;
unsigned long sim_misaligned_calls;

static int misaligned(uint64_t value)
{
	if (!(value & (sim_page_size - 1)))
		return 0;
	sim_misaligned_calls++;
	errno = EINVAL;
	return 1;
}

static size_t rounded(size_t length)
{
	return (length + sim_page_size - 1) & ~(sim_page_size - 1);
}

void *sim_mmap(void *address, size_t length, int protection, int flags, int fd, off_t offset)
{
	char *reserved, *aligned;
	size_t size = rounded(length);

	if (!length)
	{
		errno = EINVAL;
		return MAP_FAILED;
	}
	if (misaligned((uint64_t)offset))
		return MAP_FAILED;
	if (flags & (MAP_FIXED | MAP_FIXED_NOREPLACE))
	{
		if (misaligned((uint64_t)(uintptr_t)address))
			return MAP_FAILED;
		return mmap(address, size, protection, flags, fd, offset);
	}
	/* (anywhere: on a simulated page) */
	reserved = mmap(NULL, size + sim_page_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (reserved == MAP_FAILED)
		return MAP_FAILED;
	aligned = (char *)(((uintptr_t)reserved + sim_page_size - 1) & ~(uintptr_t)(sim_page_size - 1));
	if (aligned > reserved)
		munmap(reserved, (size_t)(aligned - reserved));
	if (aligned + size < reserved + size + sim_page_size)
		munmap(aligned + size, (size_t)(reserved + size + sim_page_size - (aligned + size)));
	return mmap(aligned, size, protection, flags | MAP_FIXED, fd, offset);
}

int sim_munmap(void *address, size_t length)
{
	if (misaligned((uint64_t)(uintptr_t)address))
		return -1;
	return munmap(address, rounded(length));
}

int sim_mprotect(void *address, size_t length, int protection)
{
	if (misaligned((uint64_t)(uintptr_t)address))
		return -1;
	return mprotect(address, rounded(length), protection);
}

int sim_madvise(void *address, size_t length, int advice)
{
	if (misaligned((uint64_t)(uintptr_t)address))
		return -1;
	return madvise(address, rounded(length), advice);
}

/* a byte for each simulated page: resident if any of its real pages is */
int sim_mincore(void *address, size_t length, unsigned char *vector)
{
	unsigned char real[256];
	size_t real_page = (size_t)getpagesize(), per = sim_page_size / real_page, page, index;

	if (misaligned((uint64_t)(uintptr_t)address))
		return -1;
	for (page = 0; page < rounded(length) / sim_page_size; page++)
	{
		vector[page] = 0;
		if (per > sizeof(real) || mincore((char *)address + page * sim_page_size, sim_page_size, real) != 0)
			return -1;
		for (index = 0; index < per; index++)
			vector[page] |= real[index] & 1;
	}
	return 0;
}

long sim_sysconf(int name)
{
	if (name == _SC_PAGESIZE)
		return (long)(sim_reported_page_size ? sim_reported_page_size : sim_page_size);
	return sysconf(name);
}

int sim_getpagesize(void)
{
	return (int)(sim_reported_page_size ? sim_reported_page_size : sim_page_size);
}
