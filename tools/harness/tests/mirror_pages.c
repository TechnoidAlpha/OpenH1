/* The vertex mirror's pages (port/linux/src/d3d8_device.c: mirror_refresh,
mirror_range, d3d8_mirror_rewritten, and a record's pages played back) on a
write tracking whose pages are the host's, 16 KB as on Android's and ARM
Linux's kernels that use them (or 4 KB, 64 KB), simulated: protecting any
byte of a host page protects all of it, and its generation is all of its
bytes'. The mirror's page is the tracking's (memory_watch_page_size), so a
page the game rewrites every frame beside one drawn now and then turns
volatile together with it and stops faulting; what a draw reads from the
mirror is never older than the memory; a page written each frame turns
volatile, for a while that doubles; and a record's pages, kept in 4 KB
units, play back as the mirror's pages that hold them. Case names end in
@<host page size>. */
#include "harness.h"

#include <stdint.h>

typedef int BOOL;
typedef unsigned long DWORD;
/* (the journal's state's sizes, as prepared_journal.c has them) */
#define D3DTSS_MAXSTAGES 4
#define D3DRS_MAX 144
#define D3DTSS_MAX 32

/* a small window: its 4 KB units counted, as the memory's versions */
#define PLATFORM_CONTIGUOUS_BASE 0x80000000UL
#define PLATFORM_CONTIGUOUS_SIZE 0x1000000UL
#define XGPU_MIRROR_SEGMENT_SIZE 0x400000UL
#define UNIT 0x1000UL
#define UNITS (PLATFORM_CONTIGUOUS_SIZE / UNIT)

static struct { unsigned long frame; } device;
static struct { unsigned long mirrored_bytes; } stats;
static BOOL journal_recording;
static void journal_page(unsigned long page);

/* ---------- the write tracking, at the host's pages */

static unsigned long host_page = 0x4000;
static unsigned char host_protected[UNITS];
static unsigned long host_generation[UNITS];
static unsigned long serial = 1, protections, faults;

static unsigned long memory_watch_page_size(void)
{
	return host_page;
}

static unsigned long host_index(unsigned long address)
{
	return (address - PLATFORM_CONTIGUOUS_BASE) / host_page;
}

static void memory_watch_protect(unsigned long address, unsigned long size)
{
	unsigned long page;

	for (page = host_index(address); page <= host_index(address + size - 1); page++)
	{
		if (!host_protected[page])
		{
			host_protected[page] = 1;
			protections++;
		}
	}
}

static unsigned long memory_watch_generation(unsigned long address, unsigned long size)
{
	unsigned long page, newest = 0;

	for (page = host_index(address); page <= host_index(address + size - 1); page++)
	{
		if (host_generation[page] > newest)
			newest = host_generation[page];
	}
	return newest;
}

/* the memory's contents, a version for each 4 KB unit, and the versions
the mirror's uploads took */
static unsigned long content[UNITS], uploaded[UNITS];

/* the game writes a byte: a fault if its host page is protected */
static void game_write(unsigned long address)
{
	unsigned long page = host_index(address);

	content[(address - PLATFORM_CONTIGUOUS_BASE) / UNIT]++;
	if (host_protected[page])
	{
		faults++;
		host_protected[page] = 0;
		host_generation[page] = ++serial;
	}
}

static unsigned long upload_calls, uploaded_bytes;

static void xgpu_backend_mirror_write(unsigned long segment, unsigned long offset, unsigned long size, const void *data,
	BOOL unused)
{
	unsigned long start = segment * XGPU_MIRROR_SEGMENT_SIZE + offset, unit;

	(void)data;
	(void)unused;
	CHECK(offset + size <= XGPU_MIRROR_SEGMENT_SIZE, "an upload crosses its segment's end");
	for (unit = start / UNIT; unit < (start + size) / UNIT; unit++)
		uploaded[unit] = content[unit];
	upload_calls++;
	uploaded_bytes += size;
}

#include "mirror.inc"

/* what a draw read from the mirror is the memory's as it is now */
static BOOL draw(unsigned long address, unsigned long size)
{
	long segment;
	unsigned long offset, generation, unit;

	if (!mirror_range(address, size, &segment, &offset, &generation))
		return FALSE;
	for (unit = (address - PLATFORM_CONTIGUOUS_BASE) / UNIT; unit <= (address + size - 1 - PLATFORM_CONTIGUOUS_BASE) / UNIT;
		unit++)
	{
		CHECK(uploaded[unit] == content[unit], "frame %lu: the mirror's unit %lu is older than the memory (%lu, %lu)",
			device.frame, unit, uploaded[unit], content[unit]);
	}
	return TRUE;
}

/* as the device's backend is made ready */
static void start(void)
{
	mirror_page_size_set(memory_watch_page_size());
}

/* the mirror's page is the tracking's, once */
static void case_size(void)
{
	start();
	CHECK(MIRROR_PAGE_SIZE == host_page, "the mirror's page is %lu bytes, the tracking's %lu", MIRROR_PAGE_SIZE,
		host_page);
	mirror_page_size_set(host_page * 2);
	CHECK(MIRROR_PAGE_SIZE == host_page, "changed to %lu bytes", MIRROR_PAGE_SIZE);
}

/* a page the game rewrites every frame beside one in the same host page that
is drawn every third frame: they turn volatile together, and the faults stop */
static void case_grouping(void)
{
	unsigned long dynamic = PLATFORM_CONTIGUOUS_BASE + 2 * host_page, steady = dynamic + host_page - UNIT;

	start();
	for (device.frame = 1; device.frame <= 300; device.frame++)
	{
		game_write(dynamic + 16);
		draw(dynamic, 256);
		if (device.frame % 3 == 0)
			draw(steady, 256);
	}
	CHECK(faults <= 2 * MIRROR_VOLATILE_REWRITES, "%lu faults in 300 frames (the host page protected again and again)",
		faults);
}

/* pages never written: uploaded once, protected once, no fault */
static void case_steady(void)
{
	unsigned long first = PLATFORM_CONTIGUOUS_BASE + host_page, size = 3 * host_page + 100;

	start();
	for (device.frame = 1; device.frame <= 50; device.frame++)
		CHECK(draw(first + 50, size), "frame %lu: not drawn from the mirror", device.frame);
	CHECK(faults == 0 && protections == (size + 50 + host_page - 1) / host_page,
		"%lu faults, %lu host pages protected", faults, protections);
	CHECK(uploaded_bytes == ((first + 50 + size - 1) / host_page - first / host_page + 1) * host_page,
		"%lu bytes uploaded: not the whole mirror pages once", uploaded_bytes);
	/* one write: uploaded again, current, not volatile */
	game_write(first + host_page + 8);
	CHECK(draw(first + 50, size) && faults == 1, "the written page");
	CHECK(draw(first + 50, size) && uploaded_bytes ==
		((first + 50 + size - 1) / host_page - first / host_page + 2) * host_page, "%lu bytes uploaded", uploaded_bytes);
}

/* a page written every frame is streamed for MIRROR_VOLATILE_FRAMES, then
mirrored again, and streamed for twice as long the next time */
static void case_volatile(void)
{
	unsigned long page = PLATFORM_CONTIGUOUS_BASE + 5 * host_page, turned = 0, back = 0, again = 0;

	start();
	for (device.frame = 1; device.frame <= 3000 && !again; device.frame++)
	{
		BOOL mirrored;

		game_write(page + 4);
		mirrored = draw(page, 64);
		if (!mirrored && !turned)
			turned = device.frame;
		else if (mirrored && turned && !back)
			back = device.frame;
		else if (!mirrored && back && !again)
			again = device.frame;
	}
	CHECK(turned && turned <= MIRROR_VOLATILE_REWRITES + 1, "turned volatile at frame %lu", turned);
	CHECK(back && back - turned >= MIRROR_VOLATILE_FRAMES && back - turned <= MIRROR_VOLATILE_FRAMES + 2,
		"mirrored again after %lu frames", back - turned);
	CHECK(again && again - back <= MIRROR_VOLATILE_REWRITES + 1, "volatile again after %lu frames", again - back);
	turned = again;
	for (back = 0; device.frame <= 6000 && !back; device.frame++)
	{
		game_write(page + 4);
		if (draw(page, 64))
			back = device.frame;
	}
	CHECK(back && back - turned >= 2 * MIRROR_VOLATILE_FRAMES && back - turned <= 2 * MIRROR_VOLATILE_FRAMES + 2,
		"the second time, mirrored again after %lu frames", back - turned);
}

/* a load zone's rewrite: the written pages uploaded at once, whole mirror
pages, a segment at a time */
static void case_rewritten(void)
{
	unsigned long first = XGPU_MIRROR_SEGMENT_SIZE - 2 * host_page, address = PLATFORM_CONTIGUOUS_BASE + first;
	unsigned long calls;

	start();
	device.frame = 1;
	/* (a draw is never across segments) */
	CHECK(draw(address, 2 * host_page) && draw(address + 2 * host_page, 2 * host_page), "drawn");
	game_write(address + host_page + 1);
	game_write(address + 2 * host_page + 1);
	calls = upload_calls;
	uploaded_bytes = 0;
	d3d8_mirror_rewritten(address, 4 * host_page);
	CHECK(upload_calls == calls + 2 && uploaded_bytes == 2 * host_page,
		"%lu uploads, %lu bytes, for two pages either side of a segment's end", upload_calls - calls, uploaded_bytes);
	CHECK(draw(address, 2 * host_page) && draw(address + 2 * host_page, 2 * host_page) && upload_calls == calls + 2,
		"drawn without another upload");
}

/* a record's pages, in 4 KB units: those a mirror of 4 KB pages recorded,
played back at this one's as the pages that hold them */
static unsigned long played[16][2];
static int played_count;

static void on_entry(unsigned long op, const struct journal_state *state)
{
	(void)op;
	(void)state;
}

static void on_texture(uint32_t tag_index, uint32_t bitmap_index)
{
	(void)tag_index;
	(void)bitmap_index;
}

static void on_pages(unsigned long first, unsigned long end)
{
	played[played_count][0] = first;
	played[played_count][1] = end;
	played_count++;
	journal_replay_pages(first, end);
}

static void case_replay(void)
{
	static const struct journal_reader reader = { on_entry, on_texture, on_pages };
	/* (units 5 to 8 and 1030 to 1031, as a mirror of 4 KB pages records
	them; 1024 units to a segment) */
	uint32_t words[] = { JOURNAL_PAGES | 2u << 8, 5, 8, 1030, 1031 };
	struct journal_state state;
	unsigned long per = host_page / UNIT, unit;

	start();
	memset(&state, 0, sizeof(state));
	CHECK(journal_read(words, sizeof(words) / sizeof(words[0]), &state, PLATFORM_CONTIGUOUS_SIZE, MIRROR_PAGE_SIZE,
		&reader), "the record read");
	CHECK(played_count == 2 && played[0][0] == 5 / per && played[0][1] == (8 + per - 1) / per &&
		played[1][0] == 1030 / per && played[1][1] == (1031 + per - 1) / per,
		"played back as pages %lu-%lu and %lu-%lu", played[0][0], played[0][1], played[1][0], played[1][1]);
	for (unit = 0; unit < UNITS; unit++)
	{
		BOOL held = (unit >= 5 / per * per && unit < (8 + per - 1) / per * per) ||
			(unit >= 1030 / per * per && unit < (1031 + per - 1) / per * per);

		CHECK(held == (mirror.state[unit / per] == _mirror_page_present), "unit %lu", unit);
	}
	/* recorded again, at this mirror's page: the same units, whole pages */
	journal_recording = TRUE;
	memset(&journal, 0, sizeof(journal));
	journal.pages[(5 / per) >> 3] |= (unsigned char)(1u << ((5 / per) & 7));
	journal_runs(journal.pages, MIRROR_PAGE_COUNT, MIRROR_PAGE_SIZE);
	CHECK(journal.count == 3 && journal.words[1] == 5 / per * per && journal.words[2] == (5 / per + 1) * per,
		"recorded as units %u-%u", journal.words[1], journal.words[2]);
}

int main(int argc, char **argv)
{
	char which[64];
	char *at;

	snprintf(which, sizeof(which), "%s", argc > 1 ? argv[1] : "");
	at = strchr(which, '@');
	if (at)
	{
		host_page = strtoul(at + 1, NULL, 0);
		*at = '\0';
	}
	if (!strcmp(which, "size"))
		case_size();
	else if (!strcmp(which, "grouping"))
		case_grouping();
	else if (!strcmp(which, "steady"))
		case_steady();
	else if (!strcmp(which, "volatile"))
		case_volatile();
	else if (!strcmp(which, "rewritten"))
		case_rewritten();
	else if (!strcmp(which, "replay"))
		case_replay();
	else
		CHECK(0, "no case %s", which);
	return 0;
}
