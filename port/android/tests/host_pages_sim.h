/*
HOST_PAGES_SIM.H

A kernel with larger pages than the build machine's, simulated: included
first (-include) in the host's sources under test, it sends their page
calls (mmap, munmap, mprotect, madvise, mincore) and their questions of the
page size (sysconf, getpagesize) to host_pages_sim.c, which behaves as a
kernel whose pages are sim_page_size bytes: an address that is not on one
of its pages is refused (EINVAL), and a length reaches to the end of the
last page it touches, as Android's 16 KB kernels and ARM Linux's 16 KB and
64 KB ones do. Each refused call is counted (sim_misaligned_calls): a real
kernel of that page size would have failed it.

Built by tools/test_host_pages.py.
*/

#ifndef HOST_PAGES_SIM_H
#define HOST_PAGES_SIM_H

#define mmap sim_mmap
#define munmap sim_munmap
#define mprotect sim_mprotect
#define madvise sim_madvise
#define mincore sim_mincore
#define sysconf sim_sysconf
#define getpagesize sim_getpagesize

/* the simulated kernel's page size (set before anything maps) */
extern unsigned long sim_page_size;
/* what the page size questions answer: sim_page_size, unless set (a host
that believes its pages are another size, the tests' negative control) */
extern unsigned long sim_reported_page_size;
/* calls refused for an address or offset not on a simulated page */
extern unsigned long sim_misaligned_calls;

#endif
