/*
HOST_WINDOWS.C

The host's half of what the 64-bit Windows build's guest needs beyond the
Linux desktop guest's (guest_windows.c is the guest's half,
host_imports.list lists them): the Windows functions the platform layer
calls under _WIN32 or HALO_WINDOWS, here performed for the guest.
*/

#include "host_win.h"

/* port/windows/src/win32_p2p.c */
int posix_hardware_id_source(char *text, int size);

int host_windows_hardware_id(char *text, int size)
{
	return posix_hardware_id_source(text, size);
}


/* (sdl_platform.c's frames' limit, on CLOCK_MONOTONIC: host_syscall_win.c) */
void host_windows_sleep_until(unsigned long long when)
{
	host_sleep_until_nanoseconds(when);
}
