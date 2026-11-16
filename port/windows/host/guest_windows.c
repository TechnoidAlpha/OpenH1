/*
GUEST_WINDOWS.C

The guest's half of what the 64-bit Windows build's code paths (HALO_WINDOWS
in the guest, tools/windows_build.py) need beyond the Linux desktop's
(port/linux/host/guest_desktop.c): the Windows functions the platform layer
calls under _WIN32 or HALO_WINDOWS, which the host performs here. The
host's half is host_windows.c; host_imports.list here lists them.
*/

/* ---------- the host's half (host_windows.c, host_imports.list) */

/* p2p.c's hardware id: the SMBIOS system UUID, else the registry's
MachineGuid, as text into text (size bytes); 0 if neither */
int host_windows_hardware_id(char *text, int size);
/* sdl_platform.c's frames' limit: sleeps until a time of CLOCK_MONOTONIC
(nanoseconds), on time (the timer, then a spin) */
void host_windows_sleep_until(unsigned long long when);

/* ---------- what the platform layer calls */

/* (port/linux/src/p2p.c; the host's port/windows/src/win32_p2p.c) */
int posix_hardware_id_source(char *text, int size)
{
	return host_windows_hardware_id(text, size);
}
