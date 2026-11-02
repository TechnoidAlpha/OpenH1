/*
HOST_MEMORY_WATCH.C

Guest memory write tracking (the interface of port/linux/src/memory_watch.c)
and the host's fault handlers.

The renderer write-protects the pages behind the textures it caches, and
the fault handler here records the first write to each. Other faults are
reported (with guest-relative addresses) and passed on to the previous
handler. Under ARM translation (the x86 emulator) page faults cannot be
caught, and the tracking compares page contents instead
(host_watch_hash.c).

The guest names its ranges with guest addresses (host.h); the pages
protected are the host's (4 KB on Linux and Android, 16 KB on macOS), so on
a host with larger pages a write marks a little more than it touched.

The renderer's texture cache can be on the host's render thread
(port/macos/xgpu_metal_textures.c), which protects pages and reads their
generations while the guest runs and writes them: a page's state (its
protection and generation) changes under a lock, which the fault handler
takes too (it is held only around those few stores and an mprotect, which
cannot fault).
*/

#include "host.h"
#include "host_watch_hash.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* the tracking's pages: the host's under page protection; the hashing's
(WATCH_HASH_PAGE_SIZE) otherwise */
#define WATCH_PAGE_COUNT (HALO_GUEST_WINDOW_SIZE / WATCH_HASH_PAGE_SIZE)

static uint8_t page_protected[WATCH_PAGE_COUNT];
static uint32_t page_generation[WATCH_PAGE_COUNT];
static volatile uint32_t current_generation = 1;
static int watch_active;
static int watch_hashing;
static uint8_t page_hash_watched[WATCH_PAGE_COUNT];
static uint64_t page_hash[WATCH_PAGE_COUNT];
static uint32_t page_hashed_frame[WATCH_PAGE_COUNT];
static struct watch_hash page_hashes =
{
	.page_count = WATCH_PAGE_COUNT,
	.watched = page_hash_watched,
	.hash = page_hash,
	.hashed_frame = page_hashed_frame,
	.generation = page_generation,
	.current_generation = &current_generation,
	.frame = 1,
};

/* (the window's host address, once the guest's base is known) */
static struct watch_hash *hashes(void)
{
	if (!page_hashes.base)
		page_hashes.base = (const uint8_t *)host_address(HALO_GUEST_WINDOW_BASE);
	return &page_hashes;
}

static uint64_t watch_page_size(void)
{
	return watch_hashing ? WATCH_HASH_PAGE_SIZE : host_page_size();
}

static uint64_t watch_page_count(void)
{
	return HALO_GUEST_WINDOW_SIZE / watch_page_size();
}

static int in_window(uint64_t address)
{
	return address >= HALO_GUEST_WINDOW_BASE && address - HALO_GUEST_WINDOW_BASE < HALO_GUEST_WINDOW_SIZE;
}

static uint64_t watch_page(uint64_t address)
{
	return (address - HALO_GUEST_WINDOW_BASE) / watch_page_size();
}

/* the host address of a page under page protection */
static void *page_address(uint64_t page)
{
	return (void *)host_address((uint32_t)(HALO_GUEST_WINDOW_BASE + page * host_page_size()));
}

/* the pages' state */
static volatile int page_lock;

static void pages_lock(void)
{
	while (__atomic_exchange_n(&page_lock, 1, __ATOMIC_ACQUIRE))
	{
		while (__atomic_load_n(&page_lock, __ATOMIC_RELAXED))
			;
	}
}

static void pages_unlock(void)
{
	__atomic_store_n(&page_lock, 0, __ATOMIC_RELEASE);
}

/* (pages_lock held) */
static void mark_written(uint64_t page)
{
	void *address = page_address(page);

	page_generation[page] = __sync_add_and_fetch(&current_generation, 1);
	page_protected[page] = 0;
	mprotect(address, host_page_size(), host_memory_window_protection((uintptr_t)address));
}

/* the write tracking's faults taken (host_memory_watch_faults): each a
signal through the kernel, cheap on a Mac, dear on an iPhone */
static volatile uint64_t faults_taken;

uint64_t host_memory_watch_faults(void)
{
	return __atomic_load_n(&faults_taken, __ATOMIC_RELAXED);
}

int host_memory_watch_fault(uintptr_t address)
{
	uint64_t guest;

	if (!watch_active || watch_hashing || !host_in_guest(address))
		return 0;
	guest = address - host_guest_base;
	if (!in_window(guest))
		return 0;
	pages_lock();
	if (!page_protected[watch_page(guest)])
	{
		pages_unlock();
		/* (another thread marked it written between the fault and here: the
		write goes ahead, if the page is one the guest may write) */
		return (host_memory_window_protection(address) & PROT_WRITE) != 0;
	}
	mark_written(watch_page(guest));
	pages_unlock();
	__atomic_add_fetch(&faults_taken, 1, __ATOMIC_RELAXED);
	return 1;
}

/* (the 64-bit Windows host's handlers are port/windows/host's
host_memory_watch_win.c) */
#ifndef _WIN32

static struct sigaction previous_segv, previous_bus, previous_ill;

/* the guest's code, the stack and the frame records at a crash, through the
log's sink (which neither locks nor uses stdio); the walk copies each record
without faulting (host_arch_read_memory), as the frame pointer may hold
anything at a crash. With the base register the guest's addresses are the
low 32 bits of the host's. */
static void report_crash(int signal_number, siginfo_t *information, void *context)
{
	char line[256];
	uint64_t pc, sp, fp, lr;
	uint64_t returns[24];
	int count, index;

	host_arch_context(context, &pc, &sp, &fp, &lr);
	snprintf(line, sizeof(line), "signal %d at address %p: pc %016llx lr %016llx sp %016llx fp %016llx",
		signal_number, information->si_addr, (unsigned long long)pc, (unsigned long long)lr,
		(unsigned long long)sp, (unsigned long long)fp);
	host_log(HOST_LOG_ERROR, line);
	if (host_guest_base)
	{
		snprintf(line, sizeof(line), "  guest memory at %016llx", (unsigned long long)host_guest_base);
		host_log(HOST_LOG_ERROR, line);
	}
	if (host_in_guest(pc) && (uint32_t)pc >= host_image.base && (uint32_t)pc < host_image.end)
	{
		snprintf(line, sizeof(line), "  in the guest image: addr2line -e halo_guest.elf 0x%llx 0x%llx",
			(unsigned long long)(uint32_t)pc, (unsigned long long)(uint32_t)lr);
		host_log(HOST_LOG_ERROR, line);
	}
	for (index = 0; host_arch_register_line(context, index, line, sizeof(line)); index++)
		host_log(HOST_LOG_ERROR, line);
	count = host_arch_walk_frames(fp, host_arch_read_memory, returns, (int)(sizeof(returns) / sizeof(returns[0])));
	for (index = 0; index < count; index++)
	{
		snprintf(line, sizeof(line), "  frame %2d: return %016llx", index, (unsigned long long)returns[index]);
		host_log(HOST_LOG_ERROR, line);
	}
}

static void chain(struct sigaction *previous, int signal_number, siginfo_t *information, void *context)
{
	sigaction(signal_number, previous, NULL);
	if (previous->sa_flags & SA_SIGINFO)
	{
		if (previous->sa_sigaction)
			previous->sa_sigaction(signal_number, information, context);
	}
	else if (previous->sa_handler != SIG_DFL && previous->sa_handler != SIG_IGN)
	{
		previous->sa_handler(signal_number);
	}
	/* returning re-executes the faulting instruction under the previous
	(or default) handler */
}

/* (each handler keeps the interrupted code's errno: a write tracking fault
can come between any call and its errno check) */
static void segv_handler(int signal_number, siginfo_t *information, void *context)
{
	int saved_errno = errno;

	if (host_memory_watch_fault((uintptr_t)information->si_addr))
	{
		errno = saved_errno;
		return;
	}
	report_crash(signal_number, information, context);
	chain(&previous_segv, signal_number, information, context);
	errno = saved_errno;
}

static void bus_handler(int signal_number, siginfo_t *information, void *context)
{
	int saved_errno = errno;

#ifdef __APPLE__
	/* (macOS raises SIGBUS for a write to a read-only page) */
	if (host_memory_watch_fault((uintptr_t)information->si_addr))
	{
		errno = saved_errno;
		return;
	}
#endif
	report_crash(signal_number, information, context);
	chain(&previous_bus, signal_number, information, context);
	errno = saved_errno;
}

static void ill_handler(int signal_number, siginfo_t *information, void *context)
{
	int saved_errno = errno;

	report_crash(signal_number, information, context);
	chain(&previous_ill, signal_number, information, context);
	errno = saved_errno;
}

void host_install_signal_handlers(void)
{
	struct sigaction action;

	memset(&action, 0, sizeof(action));
	action.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
	sigemptyset(&action.sa_mask);
	action.sa_sigaction = segv_handler;
	sigaction(SIGSEGV, &action, &previous_segv);
	action.sa_sigaction = bus_handler;
	sigaction(SIGBUS, &action, &previous_bus);
	action.sa_sigaction = ill_handler;
	sigaction(SIGILL, &action, &previous_ill);
}

#endif

void host_memory_watch_initialize(void)
{
	watch_active = 1;
}

void host_memory_watch_use_hashes(void)
{
	watch_hashing = 1;
}

/* the first and last tracked page of a range of the window, guest
addresses; 0 if none */
static int watch_range(uint32_t address, uint32_t size, uint64_t *first, uint64_t *last)
{
	if (!size || !in_window(address))
		return 0;
	*first = watch_page(address);
	*last = watch_page((uint64_t)address + size - 1);
	if (*last >= watch_page_count())
		*last = watch_page_count() - 1;
	return 1;
}

/* memory_watch_protect: write-protects the pages of a range, or keeps their
hash under host_memory_watch_use_hashes; a size outside the guest window
is ignored */
void host_memory_watch_protect(uint32_t address, uint32_t size)
{
	uint64_t first, last, page;

	if (!watch_active || !watch_range(address, size, &first, &last))
		return;
	if (watch_hashing)
	{
		watch_hash_protect(hashes(), (uint32_t)first, (uint32_t)last);
		return;
	}
	pages_lock();
	/* (each run of pages not protected yet in one call: a call is a system
	call and the other threads' address translations flushed, which page by
	page took milliseconds for a load zone's structure, hundreds of pages) */
	for (page = first; page <= last; page++)
	{
		uint64_t run;

		if (page_protected[page])
			continue;
		for (run = page; run <= last && !page_protected[run]; run++)
			page_protected[run] = 1;
		mprotect(page_address(page), (size_t)(run - page) * host_page_size(), PROT_READ);
		page = run - 1;
	}
	pages_unlock();
}

uint32_t host_memory_watch_serial(void)
{
	return current_generation;
}

/* memory_watch_generation: the newest generation of the pages of a range.
Under hashing this hashes the pages again, once a frame. */
uint32_t host_memory_watch_generation(uint32_t address, uint32_t size)
{
	uint64_t first, last, page;
	uint32_t newest = 0;

	if (!watch_range(address, size, &first, &last))
		return 0;
	if (watch_hashing)
		return watch_hash_generation(hashes(), (uint32_t)first, (uint32_t)last);
	for (page = first; page <= last; page++)
	{
		if (page_generation[page] > newest)
			newest = page_generation[page];
	}
	return newest;
}

/* memory_watch_prepare_write: unprotects the pages the host itself is about
to write into. Nothing under hashing: the write shows in the next hash and
there is no protection to fault on. */
void host_memory_watch_prepare_write(uint32_t address, uint32_t size)
{
	uint64_t start = address, first, last, page;

	if (!watch_active || watch_hashing || !size)
		return;
	if (start + size <= HALO_GUEST_WINDOW_BASE || start >= (uint64_t)HALO_GUEST_WINDOW_BASE + HALO_GUEST_WINDOW_SIZE)
		return;
	if (start < HALO_GUEST_WINDOW_BASE)
		start = HALO_GUEST_WINDOW_BASE;
	first = watch_page(start);
	last = watch_page((uint64_t)address + size - 1);
	if (last >= watch_page_count())
		last = watch_page_count() - 1;
	pages_lock();
	/* (each run of protected pages of one protection let go of in one call,
	as host_memory_watch_protect protects them) */
	for (page = first; page <= last; page++)
	{
		uint64_t run;
		int protection;

		if (!page_protected[page])
			continue;
		protection = host_memory_window_protection((uintptr_t)page_address(page));
		for (run = page; run <= last && page_protected[run] &&
			host_memory_window_protection((uintptr_t)page_address(run)) == protection; run++)
		{
			page_generation[run] = __sync_add_and_fetch(&current_generation, 1);
			page_protected[run] = 0;
		}
		mprotect(page_address(page), (size_t)(run - page) * host_page_size(), protection);
		page = run - 1;
	}
	pages_unlock();
}

/* memory_watch_forget: the pages of a range were remapped or reprotected, so
treat them as written and unwatched */
void host_memory_watch_forget(uint32_t address, uint32_t size)
{
	uint64_t first, last, page;

	if (!watch_range(address, size, &first, &last))
		return;
	if (watch_hashing)
	{
		watch_hash_forget(hashes(), (uint32_t)first, (uint32_t)last);
		return;
	}
	pages_lock();
	for (page = first; page <= last; page++)
	{
		page_protected[page] = 0;
		page_generation[page] = __sync_add_and_fetch(&current_generation, 1);
	}
	pages_unlock();
}

/* memory_watch_begin_frame: pages may be hashed again; nothing under page
protection */
void host_memory_watch_begin_frame(void)
{
	if (watch_hashing)
		watch_hash_begin_frame(hashes());
}
