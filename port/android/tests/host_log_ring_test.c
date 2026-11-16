/*
host_log_ring_test.c: the host's kept log lines (port/android/host/host_common.c,
log_keep and host_log_recent), which Save Report puts in host.log. The code
is taken from host_common.c by tools/test_host_units.py into
host_log_ring.inc.
*/

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HOST_LOG_INFO 4
#define HOST_LOG_WARN 5
#define HOST_LOG_ERROR 6
#define HOST_LOG_FATAL 7

#include "host_log_ring.inc"

static int failures;

static void check(int condition, const char *what)
{
	if (!condition)
	{
		printf("failed: %s\n", what);
		failures++;
	}
}

int main(void)
{
	static char buffer[200 * 1024];
	char small[64], line[128];
	uint32_t length;
	int index;

	length = host_log_recent(buffer, sizeof(buffer));
	check(length == 0 && buffer[0] == 0, "nothing kept at first");
	log_keep(HOST_LOG_INFO, "vulkan: the device");
	log_keep(HOST_LOG_ERROR, "swapchain lost");
	length = host_log_recent(buffer, sizeof(buffer));
	check(length == strlen(buffer), "the length returned");
	check(strcmp(buffer, "vulkan: the device\nerror: swapchain lost\n") == 0, "two lines, with their levels");

	/* (a small buffer: the last whole lines that fit, never part of one) */
	length = host_log_recent(small, 30);
	check(strcmp(small, "error: swapchain lost\n") == 0, "a small buffer has the last whole line");
	length = host_log_recent(small, 23);
	check(strcmp(small, "error: swapchain lost\n") == 0, "a buffer of the last line exactly");
	length = host_log_recent(small, 22);
	check(length == 0 && small[0] == 0, "a buffer smaller than the last line has none");

	/* (more than the ring holds: its oldest lines gone, the first kept whole) */
	for (index = 0; index < 5000; index++)
	{
		snprintf(line, sizeof(line), "line %d of the many", index);
		log_keep(HOST_LOG_INFO, line);
	}
	length = host_log_recent(buffer, sizeof(buffer));
	check(length <= 64 * 1024 && length > 60 * 1024, "the ring's size kept");
	check(strncmp(buffer, "line ", 5) == 0, "the first line kept is whole");
	check(strstr(buffer, "line 4999 of the many\n") != NULL && buffer[length - 1] == '\n', "the last line kept");
	check(strstr(buffer, "swapchain") == NULL, "the oldest lines gone");
	{
		int first = -1;

		snprintf(line, sizeof(line), "line %d of the many\n", sscanf(buffer, "line %d", &first) == 1 ? first + 1 : -1);
		check(first > 0 && strncmp(strchr(buffer, '\n') + 1, line, strlen(line)) == 0, "the lines in order");
	}
	if (failures)
		return 1;
	printf("host_log_ring_test: passed\n");
	return 0;
}
