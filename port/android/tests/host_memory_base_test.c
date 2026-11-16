/*
HOST_MEMORY_BASE_TEST.C

Tests of the guest's memory with the base register
(port/android/host/host_memory_base.c): the 4 GB at a base aligned to 4 GB,
and the guest's 4 KB pages on the host's pages, which are larger on macOS
(16 KB). Built and run by tools/test_host_memory_base.py; exits nonzero on
a failure.
*/

#include "host.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* Linux's flags, which the guest passes */
#define MAP_PRIVATE_ 0x02
#define MAP_FIXED_ 0x10
#define MAP_ANONYMOUS_ 0x20
#define MAP_FIXED_NOREPLACE_ 0x100000

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

/* ---------- what the unit uses of the rest of the host */

void host_logf(int priority, const char *format, ...)
{
	(void)priority;
	(void)format;
}

static uint32_t forgotten_address, forgotten_size;
static int forgotten_protection;

/* (asks the window's protection, as the write tracking does with its page
lock held: memory_lock taken there too deadlocked an mprotect, which
forgets with memory_lock held, against a write the tracking prepared) */
void host_memory_watch_forget(uint32_t address, uint32_t size)
{
	forgotten_address = address;
	forgotten_size = size;
	forgotten_protection = host_memory_window_protection(host_address(address));
}

int host_linux_errno(int error)
{
	return error;
}

/* ---------- tests */

static uint8_t *at(uint32_t address)
{
	return (uint8_t *)host_address(address);
}

static void test_reservation(void)
{
	CHECK(host_memory_initialize(HALO_GUEST_IMAGE_BASE, 0x100000) == 0);
	CHECK(host_guest_base != 0);
	CHECK((host_guest_base & 0xffffffffULL) == 0);
	CHECK(host_in_guest(host_guest_base));
	CHECK(!host_in_guest(host_guest_base + 0x100000000ULL));
}

/* the guest's anonymous memory, in 4 KB pages */
static void test_anonymous(void)
{
	long first = host_guest_mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE_ | MAP_ANONYMOUS_, -1, 0);
	long second = host_guest_mmap(0, 0x3000, PROT_READ | PROT_WRITE, MAP_PRIVATE_ | MAP_ANONYMOUS_, -1, 0);

	CHECK(first > 0 && second > 0 && first != second);
	CHECK(first >= 0x01000000 && (first & 0xfff) == 0);
	CHECK(at((uint32_t)first)[0] == 0 && at((uint32_t)second)[0x2fff] == 0);
	at((uint32_t)first)[0] = 1;
	at((uint32_t)second)[0x2fff] = 2;
	CHECK(host_low_owns(host_address((uint32_t)second), 0x3000));
	/* (musl's malloc: one 4 KB page of a mapping made accessible) */
	CHECK(host_guest_mprotect((uint64_t)second + 0x1000, 0x1000, PROT_READ) == 0);
	CHECK(at((uint32_t)second)[0x2fff] == 2);
	CHECK(host_guest_munmap((uint64_t)first, 0x1000) == 0);
	CHECK(!host_low_owns(host_address((uint32_t)first), 0x1000));
	CHECK(host_guest_munmap((uint64_t)second, 0x3000) == 0);
}

/* 4 KB pages of one host page mapped, unmapped and mapped again (the Xbox
window's blocks, port/linux/src/xbox_memory.c) */
static void test_window_pages(void)
{
	uint32_t window = HALO_GUEST_WINDOW_BASE;
	uint32_t a = window + 0x10000, b = a + 0x1000;

	CHECK(host_guest_mmap(a, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE_ | MAP_ANONYMOUS_ | MAP_FIXED_, -1, 0) == a);
	CHECK(host_guest_mmap(b, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE_ | MAP_ANONYMOUS_ | MAP_FIXED_, -1, 0) == b);
	memset(at(a), 0x11, 0x1000);
	memset(at(b), 0x22, 0x1000);
	/* the first unmapped: the second keeps its contents */
	CHECK(host_guest_munmap(a, 0x1000) == 0);
	CHECK(at(b)[0] == 0x22 && at(b)[0xfff] == 0x22);
	/* mapped again: zeroed, and the second still kept */
	CHECK(host_guest_mmap(a, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE_ | MAP_ANONYMOUS_ | MAP_FIXED_NOREPLACE_,
		-1, 0) == a);
	CHECK(at(a)[0] == 0 && at(a)[0xfff] == 0);
	CHECK(at(b)[0] == 0x22);
	/* a read-only 4 KB page beside a writable one: the host page stays
	writable (the union) */
	CHECK(host_guest_mprotect(a, 0x1000, PROT_READ) == 0);
	CHECK(host_memory_window_protection(host_address(b)) == (PROT_READ | PROT_WRITE));
	at(b)[1] = 0x33;
	CHECK(at(b)[1] == 0x33);
	/* the write tracking hears of the window's host pages that change */
	CHECK(forgotten_size == host_page_size() && forgotten_address == (a & ~(uint32_t)(host_page_size() - 1)));
	CHECK(forgotten_protection == (PROT_READ | PROT_WRITE));
	CHECK(host_guest_munmap(a, 0x2000) == 0);
	CHECK(host_memory_window_protection(host_address(a)) == PROT_NONE);
}

/* a fixed mapping outside the window, and "no replace" over it */
static void test_fixed(void)
{
	uint32_t address = 0x40440000;

	CHECK(host_guest_mmap(address, 0x20000, PROT_READ | PROT_WRITE,
		MAP_PRIVATE_ | MAP_ANONYMOUS_ | MAP_FIXED_NOREPLACE_, -1, 0) == address);
	at(address)[0x1ffff] = 7;
	CHECK(host_guest_mmap(address + 0x1000, 0x1000, PROT_READ | PROT_WRITE,
		MAP_PRIVATE_ | MAP_ANONYMOUS_ | MAP_FIXED_NOREPLACE_, -1, 0) == -EEXIST);
	/* nothing else is handed out over it */
	{
		int index;

		for (index = 0; index < 64; index++)
		{
			long other = host_guest_mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE_ | MAP_ANONYMOUS_, -1, 0);

			CHECK(other > 0 && ((uint32_t)other + 0x1000 <= address || (uint32_t)other >= address + 0x20000));
		}
	}
	CHECK(at(address)[0x1ffff] == 7);
	CHECK(host_guest_munmap(address, 0x20000) == 0);
	/* the null pages and the image are not the guest's to map */
	CHECK(host_guest_mmap(0x1000, 0x1000, PROT_READ, MAP_PRIVATE_ | MAP_ANONYMOUS_ | MAP_FIXED_, -1, 0) == -EINVAL);
	CHECK(host_guest_mmap(HALO_GUEST_IMAGE_BASE, 0x1000, PROT_READ, MAP_PRIVATE_ | MAP_ANONYMOUS_ | MAP_FIXED_, -1, 0) ==
		-EINVAL);
}

/* what a GPU buffer over guest memory needs (port/macos/xgpu_metal.m): the
readable run of host pages about a range, and a word when pages are
mapped again */
static uint32_t remapped_address, remapped_size, remapped_count;

static void remapped(uint32_t address, uint32_t size)
{
	remapped_address = address;
	remapped_size = size;
	remapped_count++;
}

static void test_gpu_ranges(void)
{
	uint32_t address = 0x40800000, page = (uint32_t)host_page_size(), start = 0, end = 0;

	CHECK(host_guest_mmap(address, 4 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE_ | MAP_ANONYMOUS_ | MAP_FIXED_NOREPLACE_,
		-1, 0) == address);
	/* the run about a range inside it, up to the limits */
	CHECK(host_memory_readable_extent(address + page + 16, 64, address - 0x100000, address + 0x100000, &start, &end));
	CHECK(start == address && end == address + 4 * page);
	CHECK(host_memory_readable_extent(address + page, 64, address + page, address + 3 * page, &start, &end));
	CHECK(start == address + page && end == address + 3 * page);
	/* not readable: a page with no access, or past the mapping */
	CHECK(host_guest_mprotect(address + 3 * page, page, PROT_NONE) == 0);
	CHECK(!host_memory_readable_extent(address + 3 * page, 16, address, address + 0x100000, &start, &end));
	CHECK(host_memory_readable_extent(address, 16, address, address + 0x100000, &start, &end) &&
		end == address + 3 * page);
	CHECK(!host_memory_readable_extent(address + 4 * page, 16, address, address + 0x100000, &start, &end));
	/* mapped afresh and unmapped: told, a host page at a time */
	host_memory_remapped = remapped;
	remapped_count = 0;
	CHECK(host_guest_mmap(address + page, page, PROT_READ | PROT_WRITE, MAP_PRIVATE_ | MAP_ANONYMOUS_ | MAP_FIXED_,
		-1, 0) == address + page);
	CHECK(remapped_count == 1 && remapped_address == address + page && remapped_size == page);
	CHECK(host_guest_munmap(address, 4 * page) == 0);
	CHECK(remapped_count == 5 && remapped_address == address + 3 * page);
	host_memory_remapped = NULL;
}

/* the host's own allocations (stacks, the boot block) */
static void test_host_allocations(void)
{
	char *memory = host_low_map(0x10000, PROT_READ | PROT_WRITE);

	CHECK(memory != NULL);
	CHECK(host_in_guest((uintptr_t)memory) && guest_address(memory) >= 0x01000000);
	memory[0] = 1;
	memory[0xffff] = 2;
	CHECK(host_low_owns((uintptr_t)memory, 0x10000));
	host_low_unmap(memory, 0x10000);
	CHECK(!host_low_owns((uintptr_t)memory, 0x10000));
}

int main(void)
{
	test_reservation();
	test_anonymous();
	test_window_pages();
	test_fixed();
	test_gpu_ranges();
	test_host_allocations();
	if (failures)
		printf("%d failures\n", failures);
	return failures != 0;
}
