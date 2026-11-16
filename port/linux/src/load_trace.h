/* Loading diagnostics written as one line, so the game's and cache thread's
records cannot split each other. The host's logger also writes whole lines. */
#ifndef HALO_LOAD_TRACE_H
#define HALO_LOAD_TRACE_H

void halo_load_trace(char const *phase, char const *detail);
/* debug.load_trace (sdl_platform.c) */
int halo_load_trace_enabled(void);

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

static void load_trace_log(const char *format, ...)
{
	char line[2048];
	va_list arguments;
	int length;
	int saved_errno = errno;

	va_start(arguments, format);
	length = vsnprintf(line, sizeof(line) - 1, format, arguments);
	va_end(arguments);
	if (length >= 0)
	{
		if ((size_t)length >= sizeof(line) - 1)
			length = sizeof(line) - 2;
		line[length++] = '\n';
#ifdef _WIN32
		(void)_write(2, line, (unsigned int)length);
#else
		(void)write(2, line, (size_t)length);
#endif
	}
	errno = saved_errno;
}

#endif
