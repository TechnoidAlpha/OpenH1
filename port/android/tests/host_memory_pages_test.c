/*
HOST_MEMORY_PAGES_TEST.C

The guest's memory (port/android/host/host_memory.c) and its write tracking
(host_memory_watch.c) on a kernel whose pages are larger than the guest's
4 KB, simulated (host_pages_sim.h: 16 KB as Android 15's kernels that use
them, or 64 KB as some ARM Linux ones, given on the command line): every
call the memory makes is on whole host pages (a real kernel of that size
would refuse the others); the guest's pages inside one host page are mapped,
unmapped, reprotected and advised each on its own, the host page having the
union of their protections, zeroed in place when one is mapped into it and
given back when none is left; a pool's, the window's and the guest's own
ranges alike; files only in whole host pages; and the write tracking
protects host pages, its generations covering them, forgetting them when
the guest's own protection changes. Built and run by
tools/test_host_pages.py; exits nonzero on a failure.
*/

#include "../host/host_memory.c"

#include <setjmp.h>
#include <sys/syscall.h>

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

/* (the guest's imports of host_memory_watch.c, which host.h does not
declare) */
void host_memory_watch_initialize(void);
void host_memory_watch_prepare_write(uint32_t address, uint32_t size);

/* ---------- the host functions host_memory.c calls */

struct host_guest_image host_image;

void host_log(int priority, const char *text)
{
	(void)priority;
	(void)text;
}

void host_logf(int priority, const char *format, ...)
{
	(void)priority;
	(void)format;
}

/* (the crash record, host_common.c's: none written here) */
int host_crash_record_begin(const char *reason)
{
	(void)reason;
	return 0;
}

void host_crash_record_line(const char *line)
{
	(void)line;
}

void host_crash_record_end(void)
{
}

/* ---------- helpers */

#define READ_WRITE (PROT_READ | PROT_WRITE)
#define ANONYMOUS (MAP_PRIVATE | MAP_ANONYMOUS)

static uint64_t unit;

static int readable(uint64_t address)
{
	uint8_t byte;

	return host_arch_read_memory((uintptr_t)address, &byte, 1);
}

static sigjmp_buf probe_jump;

static void probe_fault(int signal_number)
{
	(void)signal_number;
	siglongjmp(probe_jump, 1);
}

/* whether a byte can be written (it is written with what it holds) */
static int writable(uint64_t address)
{
	struct sigaction action, previous_segv, previous_bus;
	volatile int result = 0;

	memset(&action, 0, sizeof(action));
	action.sa_handler = probe_fault;
	sigemptyset(&action.sa_mask);
	sigaction(SIGSEGV, &action, &previous_segv);
	sigaction(SIGBUS, &action, &previous_bus);
	if (!sigsetjmp(probe_jump, 1))
	{
		volatile uint8_t *byte = (volatile uint8_t *)(uintptr_t)address;

		*byte = *byte;
		result = 1;
	}
	sigaction(SIGSEGV, &previous_segv, NULL);
	sigaction(SIGBUS, &previous_bus, NULL);
	return result;
}

static int used(const struct pool *pool, uint64_t address)
{
	return pool->used[(address - pool->base) / unit];
}

static int pool_consistent(const struct pool *pool)
{
	uint64_t page, free_pages = 0;

	for (page = 0; page < POOL_SIZE / unit; page++)
		free_pages += !pool->used[page];
	return free_pages == pool->free_pages;
}

static int aligned(uint64_t address)
{
	return !(address & (unit - 1));
}

/* ---------- tests */

static void test_page_size(void)
{
	CHECK(host_page_size() == sim_page_size);
	CHECK(host_memory_watch_page_size() == sim_page_size);
}

/* guest pages of one host page of a pool, each on its own */
static void test_pool_pages(void)
{
	long a = host_guest_mmap(0, PAGE, READ_WRITE, ANONYMOUS, -1, 0), b, c;
	struct pool *pool;

	CHECK(a > 0 && aligned((uint64_t)a));
	if (a <= 0)
		return;
	pool = pool_of((uint64_t)a, PAGE);
	CHECK(pool != NULL);
	if (!pool)
		return;
	CHECK(used(pool, a) && pool_consistent(pool));
	/* the rest of its host page is free guest pages: "no replace" takes one */
	b = host_guest_mmap((uint64_t)a + PAGE, PAGE, READ_WRITE, ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	CHECK(b == a + (long)PAGE);
	CHECK(host_guest_mmap((uint64_t)a, PAGE, READ_WRITE, ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == -EEXIST);
	CHECK(*(uint8_t *)b == 0);
	memset((void *)a, 0x11, PAGE);
	memset((void *)b, 0x22, PAGE);

	/* one unmapped: the host page stays, with the other's contents */
	CHECK(host_guest_munmap((uint64_t)a, PAGE) == 0);
	CHECK(used(pool, a) && *(uint8_t *)b == 0x22);
	/* mapped again: zeroed in place, the neighbour left alone */
	CHECK(host_guest_mmap((uint64_t)a, PAGE, READ_WRITE, ANONYMOUS | MAP_FIXED, -1, 0) == a);
	CHECK(*(uint8_t *)a == 0 && *(uint8_t *)(a + PAGE - 1) == 0 && *(uint8_t *)b == 0x22);

	/* the host page has the union of its guest pages' protections */
	CHECK(host_guest_mprotect((uint64_t)a, PAGE, PROT_READ) == 0);
	CHECK(guest_pages[a / PAGE] == (GUEST_MAPPED | PROT_READ));
	CHECK(writable((uint64_t)b) && writable((uint64_t)a));
	CHECK(host_guest_mprotect((uint64_t)b, PAGE, PROT_READ) == 0);
	CHECK(readable((uint64_t)a) && !writable((uint64_t)a) && !writable((uint64_t)b));
	CHECK(host_guest_mprotect((uint64_t)a, PAGE, READ_WRITE) == 0);
	CHECK(writable((uint64_t)b) && *(uint8_t *)b == 0x22);

	/* all of them unmapped: the host page goes back to the pool */
	CHECK(host_guest_munmap((uint64_t)a, 2 * PAGE) == 0);
	CHECK(!used(pool, a) && !readable((uint64_t)a) && pool_consistent(pool));

	/* a mapping a guest page longer than a host page: its last host page
	goes back with that page */
	c = host_guest_mmap(0, unit + PAGE, READ_WRITE, ANONYMOUS, -1, 0);
	CHECK(c > 0 && aligned((uint64_t)c));
	if (c > 0)
	{
		CHECK(used(pool_of((uint64_t)c, PAGE), c + unit));
		CHECK(host_guest_munmap((uint64_t)c + unit, PAGE) == 0);
		CHECK(!used(pool_of((uint64_t)c, PAGE), c + unit) && !readable((uint64_t)c + unit));
		CHECK(used(pool_of((uint64_t)c, PAGE), c) && readable((uint64_t)c));
		CHECK(host_guest_munmap((uint64_t)c, unit) == 0);
		CHECK(!used(pool_of((uint64_t)c, PAGE), c) && pool_consistent(pool_of((uint64_t)c, PAGE)));
	}
}

/* the host's own: whole host pages, and back to the pool */
static void test_host_low_map(void)
{
	uint8_t *m = host_low_map(100, READ_WRITE);
	struct pool *pool;

	CHECK(m != NULL && aligned((uint64_t)(uintptr_t)m));
	if (!m)
		return;
	pool = pool_of((uint64_t)(uintptr_t)m, 1);
	m[unit - 1] = 1;
	host_low_unmap(m, 100);
	CHECK(pool && !used(pool, (uint64_t)(uintptr_t)m) && !readable((uint64_t)(uintptr_t)m) && pool_consistent(pool));
}

/* "don't need" on part of a host page zeroes only that part */
static void test_madvise(void)
{
	long a = host_guest_mmap(0, unit, READ_WRITE, ANONYMOUS, -1, 0);

	CHECK(a > 0);
	if (a <= 0)
		return;
	memset((void *)a, 0x77, unit);
	CHECK(host_guest_madvise((uint64_t)a + PAGE, PAGE, MADV_DONTNEED) == 0);
	CHECK(*(uint8_t *)a == 0x77 && *(uint8_t *)(a + PAGE) == 0 && *(uint8_t *)(a + 2 * PAGE - 1) == 0);
	CHECK(*(uint8_t *)(a + unit - 1) == (unit > 2 * PAGE ? 0x77 : 0));
	CHECK(host_guest_madvise((uint64_t)a, unit, MADV_DONTNEED) == 0);
	CHECK(*(uint8_t *)a == 0 && *(uint8_t *)(a + unit - 1) == 0);
	/* (read-only guest pages too) */
	memset((void *)a, 0x78, unit);
	CHECK(host_guest_mprotect((uint64_t)a, unit, PROT_READ) == 0);
	CHECK(host_guest_madvise((uint64_t)a + PAGE, PAGE, MADV_DONTNEED) == 0);
	CHECK(*(uint8_t *)(a + PAGE) == 0 && *(uint8_t *)a == 0x78 && !writable((uint64_t)a));
	CHECK(host_guest_munmap((uint64_t)a, unit) == 0);
}

/* ranges of the guest's own, at addresses that are not on a host page */
static void test_guest_ranges(void)
{
	uint64_t gap = find_gap(4 * unit, LOW_START);
	long result;

	CHECK(gap != 0 && aligned(gap));
	if (!gap)
		return;
	result = host_guest_mmap(gap + PAGE, PAGE, READ_WRITE, ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	CHECK(result == (long)(gap + PAGE));
	if (result != (long)(gap + PAGE))
		return;
	*(uint8_t *)(uintptr_t)(gap + PAGE) = 0x55;
	/* its host page is the guest's now: the rest of it may be taken */
	CHECK(host_guest_mmap(gap + 2 * PAGE, PAGE, READ_WRITE, ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) ==
		(long)(gap + 2 * PAGE));
	CHECK(host_guest_mmap(gap + PAGE, PAGE, READ_WRITE, ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == -EEXIST);
	CHECK(host_guest_mprotect(gap + PAGE, PAGE, PROT_READ) == 0);
	CHECK(writable(gap + 2 * PAGE) && *(uint8_t *)(uintptr_t)(gap + PAGE) == 0x55);
	CHECK(host_guest_munmap(gap + PAGE, PAGE) == 0);
	CHECK(readable(gap));
	CHECK(host_guest_munmap(gap + 2 * PAGE, PAGE) == 0);
	/* none left: the host page is the system's again */
	CHECK(!readable(gap));
	CHECK(host_guest_munmap(gap + PAGE, PAGE) == -EINVAL);
	CHECK(host_guest_mprotect(gap, PAGE, PROT_READ) == -ENOMEM);
}

/* a file only in whole host pages */
static void test_file(void)
{
	int fd = (int)syscall(SYS_memfd_create, "host_memory_pages_test", 0);
	long a;

	CHECK(fd >= 0);
	if (fd < 0)
		return;
	CHECK(ftruncate(fd, (off_t)(2 * unit)) == 0);
	CHECK(pwrite(fd, "page", 4, (off_t)unit) == 4);
	CHECK(host_guest_mmap(0, PAGE, PROT_READ, MAP_PRIVATE, fd, (int64_t)PAGE) == -EINVAL);
	a = host_guest_mmap(0, PAGE, PROT_READ, MAP_PRIVATE, fd, (int64_t)unit);
	CHECK(a > 0 && aligned((uint64_t)a));
	if (a > 0)
	{
		CHECK(!memcmp((void *)a, "page", 4));
		CHECK(host_guest_mmap((uint64_t)a + PAGE, PAGE, PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, 0) == -EINVAL);
		CHECK(host_guest_munmap((uint64_t)a, PAGE) == 0);
		CHECK(!readable((uint64_t)a));
	}
	close(fd);
}

static void test_window(void)
{
	uint64_t window = HALO_GUEST_WINDOW_BASE, page = window + unit, watched = window + 2 * unit;
	uint32_t generation;
	uint64_t faults;

	/* reserved as port/linux/src/xbox_memory.c does */
	CHECK(host_guest_mmap(window, HALO_GUEST_WINDOW_SIZE, PROT_NONE, ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE,
		-1, 0) == (long)window);
	/* a block in the second guest page of a host page */
	CHECK(host_guest_mmap(page + PAGE, PAGE, READ_WRITE, ANONYMOUS | MAP_FIXED, -1, 0) == (long)(page + PAGE));
	*(uint8_t *)(uintptr_t)(page + PAGE) = 0x66;
	CHECK(host_memory_window_protection((uintptr_t)page) == READ_WRITE);
	/* one in its first: zeroed in place, the other left alone */
	CHECK(host_guest_mmap(page, PAGE, READ_WRITE, ANONYMOUS | MAP_FIXED, -1, 0) == (long)page);
	CHECK(*(uint8_t *)(uintptr_t)page == 0 && *(uint8_t *)(uintptr_t)(page + PAGE) == 0x66);
	/* freed as xbox_memory.c frees: the union is what is left */
	CHECK(host_guest_mmap(page + PAGE, PAGE, PROT_NONE, ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) ==
		(long)(page + PAGE));
	CHECK(host_memory_window_protection((uintptr_t)page) == READ_WRITE && writable(page));
	CHECK(host_guest_mmap(page, PAGE, PROT_NONE, ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) == (long)page);
	CHECK(host_memory_window_protection((uintptr_t)page) == PROT_NONE && !readable(page));

	/* the write tracking: a host page protected, its generation for all of
	its guest pages */
	CHECK(host_guest_mmap(watched, 2 * PAGE, READ_WRITE, ANONYMOUS | MAP_FIXED, -1, 0) == (long)watched);
	host_memory_watch_initialize();
	host_install_signal_handlers();
	host_memory_watch_protect((uint32_t)watched, PAGE);
	generation = host_memory_watch_generation((uint32_t)watched, PAGE);
	faults = host_memory_watch_faults();
	CHECK(!writable(watched + PAGE));
	/* (a write to the other guest page faults, and counts for the first) */
	*(volatile uint8_t *)(uintptr_t)(watched + PAGE) = 1;
	CHECK(host_memory_watch_faults() == faults + 1);
	CHECK(host_memory_watch_generation((uint32_t)watched, PAGE) > generation);
	CHECK(writable(watched));

	/* the guest's own protection of the other guest page changes the host
	page's: the tracking forgets it, and a write after is seen */
	host_memory_watch_protect((uint32_t)watched, PAGE);
	generation = host_memory_watch_generation((uint32_t)watched, PAGE);
	CHECK(host_guest_mprotect(watched + PAGE, PAGE, PROT_READ) == 0);
	*(volatile uint8_t *)(uintptr_t)watched = 2;
	CHECK(host_memory_watch_generation((uint32_t)watched, PAGE) > generation);
	/* and the host page's protection is the guest's again */
	CHECK(writable(watched) && host_memory_window_protection((uintptr_t)watched) == READ_WRITE);

	/* a write the host makes, announced: the host page unprotected, with
	the guest's protection */
	host_memory_watch_protect((uint32_t)watched, PAGE);
	generation = host_memory_watch_generation((uint32_t)watched, PAGE);
	host_memory_watch_prepare_write((uint32_t)(watched + PAGE), 16);
	CHECK(host_memory_watch_generation((uint32_t)watched, PAGE) > generation && writable(watched));

	CHECK(host_guest_munmap(window, HALO_GUEST_WINDOW_SIZE) == 0);
	CHECK(!readable(watched));
}

static void test_image(void)
{
	uint64_t image = HALO_GUEST_IMAGE_BASE;

	/* in whole host pages only, as the kernel's */
	if (unit > PAGE)
		CHECK(host_guest_mprotect(image, PAGE, READ_WRITE) == -EINVAL);
	CHECK(host_guest_mprotect(image, unit, READ_WRITE) == 0);
	CHECK(readable(image));
	CHECK(host_guest_mprotect(image, unit, PROT_NONE) == 0);
	CHECK(host_guest_munmap(image, PAGE) == -EINVAL);
}

int main(int argc, char **argv)
{
	if (argc > 1)
		sim_page_size = strtoul(argv[1], NULL, 0);
	/* (the negative control: a host that believes its pages are 4 KB) */
	if (argc > 2)
		sim_reported_page_size = strtoul(argv[2], NULL, 0);
	if (host_memory_initialize(HALO_GUEST_IMAGE_BASE, 0x10000) != 0)
	{
		printf("FAIL: cannot reserve the window and the image's range\n");
		return 1;
	}
	unit = host_page_size();
	test_page_size();
	test_pool_pages();
	test_host_low_map();
	test_madvise();
	test_guest_ranges();
	test_file();
	test_window();
	test_image();
	CHECK(sim_misaligned_calls == 0);
	if (failures)
	{
		printf("%d failures (pages of %lu bytes; %lu calls a kernel of them refuses)\n", failures, sim_page_size,
			sim_misaligned_calls);
		return 1;
	}
	printf("host_memory with pages of %lu bytes: all passed\n", sim_page_size);
	return 0;
}
