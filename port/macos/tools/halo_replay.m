/*
HALO_REPLAY.M

The renderer's command stream replayed from a recording (HALO_RENDER_RECORD:
port/android/host/host_render_record.h), without the game: no guest, no
maps. The guest's memory is a reservation of 4 GB at a base, as the game's
host has it (host_memory_base.c); the tool is the guest, writing the
recorded records into the ring where they were and submitting them as the
encoder did (a frame's end, a synchronous record, the ring full), and the
same render thread, decoder and Metal backend play them. Before each record
is played, the memory it reads (the recording's chunks) is put back where
it was, and the pages that changed count as written for the textures'
cache (the write tracking's generations, here).

halo_replay <file> [--frames a-b] [--window] [--unpaced] [--shots dir]
	[--shot-frames list] [--loop N] [--cold] [--archive path] [--quiet]

--frames a-b: the frames measured (those before are played unmeasured, the
stream's state being theirs; none after); --window: in a window, paced by
the display unless --unpaced (offscreen, the frames are as fast as the GPU);
--shots dir: the back buffers of the frames of --shot-frames (as
HALO_RENDER_SHOT_FRAMES; all measured frames if not given) as PNGs, in
dir/loop<n> with --loop; --loop N: the whole replayed N times in one
process; --cold: in a throwaway app of a new bundle id (a new Metal shader
cache) with an empty pipeline archive; --archive: the pipelines'
archive (default: <file>.pipelines); --hitch ms: a hitch is an interval
over this (default 12.5: 1.5 refreshes at 120 Hz) and twice the median. --compare a b: the PNGs of two shot
folders compared, pixel by pixel.

Prints each measured frame: its interval (the render thread's, from frame
end to frame end), the render thread's CPU time, the GPU time of the command
buffers completed in it (a frame or two late), its draws and the pipelines
made (compiles) in it; then a summary, hitches named.
*/

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

/* (host.h's host_page_size is Mach's too: not used here) */
#define host_page_size host_page_size_of_guest_memory
#include "host.h"
#undef host_page_size
#include "host_render_record.h"
#include "host_render_stream.h"
#include "xgpu_metal.h"
#include "xgpu_metal_internal.h"
#include "xgpu_metal_stream.h"
#include "xgpu_metal_textures.h"
#include "xgpu_astc.h"

#include <SDL3/SDL.h>
#include <compression.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* host_metal.m */
int host_metal_initialize(void);
int host_metal_window_open(uint32_t window);
void host_metal_set_unpaced(int unpaced);

/* ---------- what the host gives the backend and the decoder, here */

uintptr_t host_guest_base;
void (*host_memory_remapped)(uint32_t address, uint32_t size);
/* display.astc_textures, from HALO_ASTC_TEXTURES as the host reads it (main) */
char host_astc_textures[16] = "auto";
static int verbose;

void host_logf(int priority, const char *format, ...)
{
	va_list arguments;

	if (priority < HOST_LOG_WARN && !verbose)
		return;
	fprintf(stderr, "host: ");
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
}

void host_fatal(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
	exit(2);
}

static SDL_Window *window;

void *host_sdl_handle_get(uint32_t handle, int type)
{
	(void)type;
	return handle == 1 ? (void *)window : NULL;
}

/* the guest's memory: pages put back (readable) and their generations */
static size_t page_size;
static uint8_t *pages_restored;
static uint32_t *page_generations;
static _Atomic uint32_t watch_serial = 1;

size_t guest_host_page_size(void) __asm__("_host_page_size");

size_t guest_host_page_size(void)
{
	return page_size;
}

/* readable if every page has been put back; the extent is none, so that
the backend copies what it would read in place (the tool rewrites the
memory while frames are in flight, which the game does not) */
int host_memory_readable_extent(uint32_t address, uint32_t size, uint32_t limit_start, uint32_t limit_end,
	uint32_t *start, uint32_t *end)
{
	uint64_t page, last = ((uint64_t)address + size + page_size - 1) / page_size;

	(void)limit_start;
	(void)limit_end;
	if (!address)
		return 0;
	for (page = address / page_size; page < last; page++)
	{
		if (page >= 0x100000000ull / page_size || !pages_restored[page])
			return 0;
	}
	*start = *end = 0;
	return 1;
}

void host_memory_watch_protect(uint32_t address, uint32_t size)
{
	(void)address;
	(void)size;
}

uint32_t host_memory_watch_generation(uint32_t address, uint32_t size)
{
	uint64_t page, last = ((uint64_t)address + size + page_size - 1) / page_size;
	uint32_t newest = 0;

	for (page = address / page_size; page < last && page < 0x100000000ull / page_size; page++)
	{
		if (page_generations[page] > newest)
			newest = page_generations[page];
	}
	return newest;
}

uint32_t host_memory_watch_serial(void)
{
	return atomic_load(&watch_serial);
}

/* (no write tracking here: no faults) */
uint64_t host_memory_watch_faults(void)
{
	return 0;
}

static void pages_mark(uint32_t address, uint32_t size, int written)
{
	uint64_t page, last = ((uint64_t)address + size + page_size - 1) / page_size;
	uint32_t serial = written ? atomic_fetch_add(&watch_serial, 1) + 1 : 0;

	for (page = address / page_size; page < last && page < 0x100000000ull / page_size; page++)
	{
		pages_restored[page] = 1;
		if (written)
			page_generations[page] = serial;
	}
}

/* ---------- the recording */

struct chunk
{
	uint32_t address, size;
	const unsigned char *data;
};

struct record
{
	uint32_t position, type, size, bytes;
	const unsigned char *data;
	uint32_t chunk_first, chunk_count;
};

static struct
{
	const unsigned char *map;
	size_t map_size;
	/* the blocks' bytes, together: the events */
	unsigned char *body;
	size_t body_size;
	struct host_render_record_file header;
	struct record *records;
	uint32_t record_count;
	struct chunk *chunks;
	uint32_t chunk_count;
	/* the contents by hash (open table) */
	uint64_t *hashes;
	const unsigned char **contents;
	uint32_t content_mask;
	uint32_t frames;
	uint64_t content_bytes;
} recording;

static const unsigned char *content_find(uint64_t hash)
{
	uint32_t index;

	for (index = (uint32_t)hash & recording.content_mask; recording.hashes[index];
		index = (index + 1) & recording.content_mask)
	{
		if (recording.hashes[index] == hash)
			return recording.contents[index];
	}
	return NULL;
}

static void content_put(uint64_t hash, const unsigned char *data)
{
	uint32_t index;

	for (index = (uint32_t)hash & recording.content_mask; recording.hashes[index];
		index = (index + 1) & recording.content_mask)
	{
		if (recording.hashes[index] == hash)
			return;
	}
	recording.hashes[index] = hash;
	recording.contents[index] = data;
}

static int recording_load(const char *path)
{
	int descriptor = open(path, O_RDONLY);
	struct stat status;
	const unsigned char *place, *end;
	uint32_t records = 0, chunks = 0, contents = 0, record_capacity, chunk_capacity, size;
	uint32_t pending_first = 0;

	if (descriptor < 0 || fstat(descriptor, &status) || (size_t)status.st_size < sizeof(recording.header))
	{
		fprintf(stderr, "halo_replay: cannot read %s\n", path);
		return 0;
	}
	recording.map_size = (size_t)status.st_size;
	recording.map = mmap(NULL, recording.map_size, PROT_READ, MAP_PRIVATE, descriptor, 0);
	close(descriptor);
	if (recording.map == MAP_FAILED)
		return 0;
	memcpy(&recording.header, recording.map, sizeof(recording.header));
	if (recording.header.magic != HOST_RENDER_RECORD_MAGIC || recording.header.version != HOST_RENDER_RECORD_VERSION ||
		recording.header.header_size != sizeof(recording.header) ||
		recording.header.ring.version != XGPU_STREAM_VERSION)
	{
		fprintf(stderr, "halo_replay: %s is not a recording of this version (%u)\n", path, recording.header.version);
		return 0;
	}
	/* the blocks, decompressed */
	{
		const unsigned char *block_place = recording.map + sizeof(recording.header), *map_end = recording.map +
			recording.map_size;
		size_t total = 0;

		for (place = block_place; place + sizeof(struct host_render_record_block) <= map_end;)
		{
			struct host_render_record_block block;

			memcpy(&block, place, sizeof(block));
			if ((size_t)(map_end - place) - sizeof(block) < block.stored)
				break;
			total += block.size;
			place += sizeof(block) + block.stored;
		}
		recording.body = malloc(total ? total : 1);
		if (!recording.body)
			return 0;
		for (place = block_place; place + sizeof(struct host_render_record_block) <= map_end;)
		{
			struct host_render_record_block block;

			memcpy(&block, place, sizeof(block));
			if ((size_t)(map_end - place) - sizeof(block) < block.stored)
			{
				fprintf(stderr, "halo_replay: the recording is cut short (its last %zu bytes)\n", (size_t)(map_end - place));
				break;
			}
			if (block.stored == block.size)
				memcpy(recording.body + recording.body_size, place + sizeof(block), block.size);
			else if (compression_decode_buffer(recording.body + recording.body_size, block.size, place + sizeof(block),
				block.stored, NULL, COMPRESSION_LZ4) != block.size)
			{
				fprintf(stderr, "halo_replay: a block does not decompress\n");
				return 0;
			}
			recording.body_size += block.size;
			place += sizeof(block) + block.stored;
		}
	}
	/* (counted first, then indexed) */
	end = recording.body + recording.body_size;
	for (place = recording.body; place + sizeof(struct host_render_record_event) <= end;)
	{
		struct host_render_record_event event;

		memcpy(&event, place, sizeof(event));
		if ((size_t)(end - place) - sizeof(event) < event.size)
			break;
		records += event.type == HOST_RENDER_EVENT_RECORD;
		chunks += event.type == HOST_RENDER_EVENT_CHUNK;
		contents += event.type == HOST_RENDER_EVENT_CONTENT;
		place += sizeof(event) + event.size;
	}
	record_capacity = records ? records : 1;
	chunk_capacity = chunks ? chunks : 1;
	recording.records = calloc(record_capacity, sizeof(*recording.records));
	recording.chunks = calloc(chunk_capacity, sizeof(*recording.chunks));
	for (size = 16; size < contents * 2 + 2; size *= 2)
		;
	recording.content_mask = size - 1;
	recording.hashes = calloc(size, sizeof(*recording.hashes));
	recording.contents = calloc(size, sizeof(*recording.contents));
	if (!recording.records || !recording.chunks || !recording.hashes || !recording.contents)
		return 0;
	for (place = recording.body; place + sizeof(struct host_render_record_event) <= end;)
	{
		struct host_render_record_event event;
		const unsigned char *body = place + sizeof(event);

		memcpy(&event, place, sizeof(event));
		if ((size_t)(end - place) - sizeof(event) < event.size)
		{
			fprintf(stderr, "halo_replay: the recording is cut short (its last %zu bytes)\n", (size_t)(end - place));
			break;
		}
		place = body + event.size;
		if (event.type == HOST_RENDER_EVENT_CONTENT && event.size >= 8)
		{
			uint64_t hash;

			memcpy(&hash, body, 8);
			content_put(hash, body + 8);
			recording.content_bytes += event.size - 8;
		}
		else if (event.type == HOST_RENDER_EVENT_CHUNK && event.size >= sizeof(struct host_render_record_chunk))
		{
			struct host_render_record_chunk chunk;
			struct chunk *to = &recording.chunks[recording.chunk_count];

			memcpy(&chunk, body, sizeof(chunk));
			to->address = chunk.address;
			to->size = chunk.size;
			to->data = content_find((uint64_t)chunk.hash[0] | (uint64_t)chunk.hash[1] << 32);
			if (!to->data)
			{
				fprintf(stderr, "halo_replay: a chunk of %u bytes at %08x has no content\n", chunk.size, chunk.address);
				continue;
			}
			recording.chunk_count++;
		}
		else if (event.type == HOST_RENDER_EVENT_RECORD && event.size >= 8)
		{
			struct record *to = &recording.records[recording.record_count++];
			uint32_t header;

			memcpy(&to->position, body, 4);
			memcpy(&header, body + 4, 4);
			to->data = body + 4;
			to->bytes = event.size - 4;
			to->type = XGPU_STREAM_HEADER_TYPE(header);
			to->size = XGPU_STREAM_HEADER_SIZE(header);
			if (to->type == XGPU_STREAM_SKIP && !to->size)
				to->size = recording.header.ring.size - (to->position & (recording.header.ring.size - 1));
			to->chunk_first = pending_first;
			to->chunk_count = recording.chunk_count - pending_first;
			pending_first = recording.chunk_count;
			recording.frames += to->type == XGPU_STREAM_FRAME_END;
		}
	}
	return recording.record_count > 0;
}

/* ---------- the guest's memory */

static int memory_make(void)
{
	const struct xgpu_stream_ring *ring = &recording.header.ring;
	uintptr_t reservation;
	uint64_t pages;

	page_size = (size_t)getpagesize();
	if (recording.header.page_size && recording.header.page_size != page_size)
		fprintf(stderr, "halo_replay: recorded with %u-byte pages, replayed with %zu\n", recording.header.page_size,
			page_size);
	pages = 0x100000000ull / page_size;
	pages_restored = calloc(pages, 1);
	page_generations = calloc(pages, sizeof(*page_generations));
	/* (4 GB-aligned, as host_memory_base.c places it) */
	reservation = (uintptr_t)mmap(NULL, 0x200000000ull, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
	if ((void *)reservation == MAP_FAILED || !pages_restored || !page_generations)
		return 0;
	host_guest_base = (reservation + 0xffffffffull) & ~(uintptr_t)0xffffffffull;
	if (mmap((void *)host_guest_base, 0x100000000ull, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED |
		MAP_NORESERVE, -1, 0) == MAP_FAILED)
		return 0;
	/* the ring's header, its bytes and the host's answers, where they were */
	pages_mark(ring->buffer, ring->size, 0);
	pages_mark(ring->visibility, XGPU_STREAM_VISIBILITY_SLOTS * 2 * 4, 0);
	pages_mark(ring->pipelines, XGPU_STREAM_PIPELINES, 0);
	return 1;
}

static uint32_t ring_address(void)
{
	return recording.header.ring_address;
}

/* ---------- playing: the render thread's side */

struct frame_measure
{
	double interval_ms, cpu_ms, gpu_ms, compile_ms, shader_ms;
	uint32_t draws, compiles, shaders;
};

static struct
{
	/* the records played (of play_count each loop), and the loop */
	uint64_t played;
	uint32_t play_count;
	uint32_t loop, loops;
	uint64_t shift;
	uint32_t first_frame, last_frame;
	uint32_t frame;
	int frame_closing;
	double last_end;
	struct frame_measure *frames;
	const char *shots, *shot_frames;
	int window, unpaced, quiet;
	/* a hitch: an interval over this (and twice the median) */
	double hitch_ms;
	uint32_t mismatches;
	uint64_t restored_bytes;
	/* the textures' cache over the loop: its bytes at the most and at the
	end (all, ASTC, DXT decoded), and its counts (xgpu_metal_textures.h) */
	struct xgpu_metal_texture_statistics textures, textures_most;
} play;

static double seconds_now(void)
{
	return CACurrentMediaTime();
}

/* (the render thread) a frame's end has been played */
static void frame_closed(void)
{
	struct xgpu_metal_statistics statistics;
	double now = seconds_now();
	uint32_t frame = play.frame;

	xgpu_metal_statistics(&statistics);
	{
		struct xgpu_metal_texture_statistics textures;

		xgpu_metal_textures_statistics(&textures);
		play.textures.astc_transcoded += textures.astc_transcoded;
		play.textures.astc_encoded += textures.astc_encoded;
		play.textures.astc_left_decoded += textures.astc_left_decoded;
		play.textures.resident = textures.resident;
		play.textures.resident_astc = textures.resident_astc;
		play.textures.resident_decoded = textures.resident_decoded;
		play.textures.astc_waiting = textures.astc_waiting;
		if (textures.resident > play.textures_most.resident)
			play.textures_most = textures;
	}
	if (frame >= play.first_frame && frame <= play.last_frame)
	{
		struct frame_measure *measure = &play.frames[(size_t)play.loop * (play.last_frame - play.first_frame + 1) +
			(frame - play.first_frame)];

		measure->interval_ms = (now - play.last_end) * 1000.0;
		measure->cpu_ms = statistics.render_cpu_seconds * 1000.0;
		measure->gpu_ms = statistics.gpu_seconds * 1000.0;
		measure->compile_ms = statistics.pipeline_seconds * 1000.0;
		measure->draws = statistics.draws;
		measure->compiles = statistics.pipelines_made;
		measure->shader_ms = statistics.shader_seconds * 1000.0;
		measure->shaders = statistics.shaders_made;
	}
	play.last_end = now;
	play.frame++;
	if (play.window && play.frame == play.first_frame)
		host_metal_set_unpaced(play.unpaced);
}

static void loop_begin(uint32_t loop)
{
	char directory[1100];

	play.loop = loop;
	play.frame = 0;
	host_render_record_set_frame(0);
	if (play.window)
		host_metal_set_unpaced(play.first_frame > 0 ? 1 : play.unpaced);
	if (play.shots)
	{
		if (play.loops > 1)
		{
			mkdir(play.shots, 0755);
			snprintf(directory, sizeof(directory), "%s/loop%u", play.shots, loop);
		}
		else
		{
			snprintf(directory, sizeof(directory), "%s", play.shots);
		}
		host_render_record_shots(directory, play.shot_frames);
	}
	play.last_end = seconds_now();
}

static void restore(uint32_t position, uint32_t type, const unsigned char *bytes)
{
	const struct record *record;
	uint32_t index;

	(void)bytes;
	if (play.frame_closing)
	{
		play.frame_closing = 0;
		frame_closed();
	}
	index = (uint32_t)(play.played % play.play_count);
	record = &recording.records[index];
	if ((uint32_t)(record->position + play.shift * (play.played / play.play_count)) != position)
	{
		/* (the tool's own skips between loops) */
		if (type != XGPU_STREAM_SKIP && play.mismatches++ < 8)
			fprintf(stderr, "halo_replay: record %u played at %u, recorded at %u\n", index, position, record->position);
		return;
	}
	if (index == 0)
		loop_begin((uint32_t)(play.played / play.play_count));
	play.played++;
	for (uint32_t c = 0; c < record->chunk_count; c++)
	{
		const struct chunk *chunk = &recording.chunks[record->chunk_first + c];
		void *to = host_pointer(chunk->address);

		if (memcmp(to, chunk->data, chunk->size))
		{
			memcpy(to, chunk->data, chunk->size);
			pages_mark(chunk->address, chunk->size, 1);
			play.restored_bytes += chunk->size;
		}
		else
		{
			pages_mark(chunk->address, chunk->size, 0);
		}
	}
	if (type == XGPU_STREAM_FRAME_END)
		play.frame_closing = 1;
}

/* ---------- playing: the guest's side */

static struct xgpu_stream_ring *ring;
/* the end of the records written so far (all of them may be played) */
static uint32_t written;

static void submit(uint32_t position, uint32_t flags)
{
	__atomic_store_n(&ring->committed, position, __ATOMIC_RELEASE);
	host_render_stream_submit(flags);
}

/* waits until the ring's bytes [position, position + size) are free (what
is written so far submitted: a wait for room that only records written and
not yet submitted would free never ends) */
static void room(uint32_t position, uint32_t size)
{
	while (position + size - __atomic_load_n(&ring->released, __ATOMIC_ACQUIRE) > ring->size)
	{
		submit(written, 0);
		host_render_stream_wait(position + size - ring->size);
	}
}

static void skip_write(uint32_t position, uint32_t size)
{
	uint32_t header = XGPU_STREAM_HEADER(XGPU_STREAM_SKIP, size);

	room(position, size ? size : ring->size - (position & (ring->size - 1)));
	memcpy((unsigned char *)host_pointer(ring->buffer) + (position & (ring->size - 1)), &header, 4);
	written = position + (size ? size : ring->size - (position & (ring->size - 1)));
}

static int synchronous(uint32_t type)
{
	return type == XGPU_STREAM_INITIALIZE || type == XGPU_STREAM_READ_TARGET || type == XGPU_STREAM_VR_SUPPORTED ||
		type == XGPU_STREAM_VR_INITIALIZE;
}

static void play_all(void)
{
	uint32_t mask = ring->size - 1, loop, index;
	uint32_t position = recording.records[0].position;

	for (loop = 0; loop < play.loops; loop++)
	{
		uint32_t start = (uint32_t)(recording.records[0].position + play.shift * loop);

		/* (to the next loop's first position: skips over the gap) */
		while (position != start)
		{
			uint32_t offset = position & mask, gap = start - position;

			if (offset + gap > ring->size || gap > XGPU_STREAM_RECORD_MAXIMUM)
			{
				skip_write(position, 0);
				position += ring->size - offset;
			}
			else
			{
				skip_write(position, gap);
				position += gap;
			}
		}
		for (index = 0; index < play.play_count; index++)
		{
			const struct record *record = &recording.records[index];
			uint32_t at = (uint32_t)(record->position + play.shift * loop);

			room(at, record->size);
			memcpy((unsigned char *)host_pointer(ring->buffer) + (at & mask), record->data, record->bytes);
			position = written = at + record->size;
			if (record->type == XGPU_STREAM_FRAME_END)
			{
				ring->frames_submitted++;
				submit(position, XGPU_STREAM_SUBMIT_FRAME);
			}
			else if (synchronous(record->type))
			{
				submit(position, XGPU_STREAM_SUBMIT_SYNC);
			}
		}
		submit(position, XGPU_STREAM_SUBMIT_SYNC);
	}
}

/* ---------- the report */

static int double_compare(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return x < y ? -1 : x > y;
}

static void report(uint32_t loop, double seconds)
{
	uint32_t count = play.last_frame - play.first_frame + 1, index, hitches = 0, compiles = 0, shaders = 0;
	double shader_ms = 0;
	struct frame_measure *frames = &play.frames[(size_t)loop * count];
	double *sorted = malloc(sizeof(double) * count), median, threshold, cpu = 0, gpu = 0, compile_ms = 0, sum = 0;
	char hitch_list[512] = "";
	size_t used = 0;

	for (index = 0; index < count; index++)
		sorted[index] = frames[index].interval_ms;
	qsort(sorted, count, sizeof(double), double_compare);
	median = sorted[count / 2];
	threshold = median * 2.0 > play.hitch_ms ? median * 2.0 : play.hitch_ms;
	for (index = 0; index < count; index++)
	{
		const struct frame_measure *frame = &frames[index];
		/* (a shot's frame waits for its picture's PNG: not counted) */
		int shot = play.shots && host_render_record_shot_wanted(play.first_frame + index);
		int hitch = !shot && frame->interval_ms > threshold;

		if (!play.quiet)
			printf("frame %6u  %7.2f ms  cpu %6.2f  gpu %6.2f  draws %5u  pipelines %3u (%6.1f ms)  shaders %3u (%6.1f ms)%s\n",
				play.first_frame + index, frame->interval_ms, frame->cpu_ms, frame->gpu_ms, frame->draws, frame->compiles,
				frame->compile_ms, frame->shaders, frame->shader_ms, hitch ? "  HITCH" : shot ? "  (shot)" : "");
		if (hitch)
		{
			hitches++;
			if (used < sizeof(hitch_list) - 48)
				used += (size_t)snprintf(hitch_list + used, sizeof(hitch_list) - used, frame->shaders || frame->compiles ?
					" %u (%.1f ms; compiling %.1f)" : " %u (%.1f ms)", play.first_frame + index, frame->interval_ms,
					frame->shader_ms + frame->compile_ms);
		}
		cpu += frame->cpu_ms;
		gpu += frame->gpu_ms;
		compiles += frame->compiles;
		compile_ms += frame->compile_ms;
		shaders += frame->shaders;
		shader_ms += frame->shader_ms;
		sum += frame->interval_ms;
	}
	printf("loop %u: frames %u-%u (%u) in %.2f s (%.1f fps; the whole replay %.2f s): interval mean %.2f ms, median %.2f, "
		"p95 %.2f, p99 %.2f, max %.2f; render CPU %.2f ms a frame, GPU %.2f; pipelines made %u (%.1f ms), shaders "
		"compiled %u (%.1f ms); hitches %u (over %.2f ms)%s\n",
		loop, play.first_frame, play.last_frame, count, sum / 1000.0, count * 1000.0 / sum, seconds, sum / count, median,
		sorted[(size_t)(count * 0.95)], sorted[(size_t)(count * 0.99)], sorted[count - 1], cpu / count, gpu / count,
		compiles, compile_ms, shaders, shader_ms, hitches, threshold, hitches ? ":" : "");
	if (hitches)
		printf("  hitches:%s%s\n", hitch_list, used >= sizeof(hitch_list) - 48 ? " ..." : "");
	{
		struct xgpu_astc_statistics astc;

		xgpu_astc_statistics(&astc);
		printf("  textures: at the most %.1f MB (DXT as ASTC %.1f MB, DXT decoded %.1f MB), at the end %.1f MB "
			"(%.1f, %.1f); DXT textures transcoded to ASTC %u, encoded blocks applied %u, left decoded %u, waiting %u; "
			"the encoder: %u jobs, %u from the disk cache, %llu blocks in %.2f s\n",
			play.textures_most.resident / 1048576.0, play.textures_most.resident_astc / 1048576.0,
			play.textures_most.resident_decoded / 1048576.0, play.textures.resident / 1048576.0,
			play.textures.resident_astc / 1048576.0, play.textures.resident_decoded / 1048576.0,
			play.textures.astc_transcoded, play.textures.astc_encoded, play.textures.astc_left_decoded,
			play.textures.astc_waiting, astc.jobs, astc.cache_hits, (unsigned long long)astc.blocks_encoded,
			astc.encode_seconds);
	}
	free(sorted);
}

/* ---------- shots compared */

static unsigned char *png_read(const char *path, size_t *width, size_t *height)
{
	NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
	CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)url, NULL);
	CGImageRef image = source ? CGImageSourceCreateImageAtIndex(source, 0, NULL) : NULL;
	unsigned char *pixels = NULL;

	if (image)
	{
		CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
		CGContextRef context;

		*width = CGImageGetWidth(image);
		*height = CGImageGetHeight(image);
		pixels = calloc(*width * *height, 4);
		context = CGBitmapContextCreate(pixels, *width, *height, 8, *width * 4, space,
			kCGBitmapByteOrder32Little | (CGBitmapInfo)kCGImageAlphaNoneSkipFirst);
		CGContextDrawImage(context, CGRectMake(0, 0, *width, *height), image);
		CGContextRelease(context);
		CGColorSpaceRelease(space);
		CGImageRelease(image);
	}
	if (source)
		CFRelease(source);
	return pixels;
}

static int compare(const char *first, const char *second)
{
	NSArray<NSString *> *names = [[[NSFileManager defaultManager] contentsOfDirectoryAtPath:@(first) error:nil]
		sortedArrayUsingSelector:@selector(compare:)];
	int compared = 0, differ = 0;

	for (NSString *name in names)
	{
		size_t width_a = 0, height_a = 0, width_b = 0, height_b = 0, index, different = 0;
		unsigned char *a, *b;
		int most = 0;

		if (![name hasSuffix:@".png"])
			continue;
		a = png_read([@(first) stringByAppendingPathComponent:name].UTF8String, &width_a, &height_a);
		b = png_read([@(second) stringByAppendingPathComponent:name].UTF8String, &width_b, &height_b);
		compared++;
		if (!a || !b || width_a != width_b || height_a != height_b)
		{
			printf("%s: %s\n", name.UTF8String, !b ? "missing in the second" : "of another size");
			differ++;
		}
		else
		{
			for (index = 0; index < width_a * height_a; index++)
			{
				int channel, pixel_differs = 0;

				for (channel = 0; channel < 3; channel++)
				{
					int delta = abs((int)a[index * 4 + channel] - (int)b[index * 4 + channel]);

					if (delta)
						pixel_differs = 1;
					if (delta > most)
						most = delta;
				}
				different += pixel_differs;
			}
			printf("%s: %s (%zu of %zu pixels differ, by at most %d)\n", name.UTF8String,
				different ? "DIFFERENT" : "identical", different, width_a * height_a, most);
			differ += different != 0;
		}
		free(a);
		free(b);
	}
	printf("%d compared, %d differ\n", compared, differ);
	return compared && !differ ? 0 : 1;
}

/* ---------- --cold: this program again, in an app of a new bundle id

Metal keeps its compiled shaders by the app's bundle id (the user's cache
folder's <bundle id>/com.apple.metal); a program outside an app shares
one cache with every other (a copy under another name compiles nothing). A
throwaway app of a new id, with an empty pipeline archive, starts with
none; it and its cache are removed afterwards. */

static int cold_run(int argc, char *argv[])
{
	char self[4096], source_library[4200], identifier[128], cache[4400];
	uint32_t size = sizeof(self);
	char **arguments = calloc((size_t)argc + 4, sizeof(char *));
	int index, count = 0, has_archive = 0, status = 2;
	pid_t child;
	char *slash;
	NSFileManager *files = [NSFileManager defaultManager];
	NSString *app, *macos, *copy, *archive, *plist;

	if (_NSGetExecutablePath(self, &size) || !arguments)
		return 2;
	snprintf(identifier, sizeof(identifier), "com.openh1.halo-replay.cold-%ld-%d", (long)time(NULL), getpid());
	app = [NSTemporaryDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"%s.app", identifier]];
	macos = [app stringByAppendingPathComponent:@"Contents/MacOS"];
	copy = [macos stringByAppendingPathComponent:@"halo_replay"];
	archive = [app stringByAppendingPathComponent:@"pipelines.bin"];
	snprintf(source_library, sizeof(source_library), "%s", self);
	slash = strrchr(source_library, '/');
	if (slash)
		snprintf(slash + 1, sizeof(source_library) - (size_t)(slash + 1 - source_library), "libSDL3.0.dylib");
	plist = [NSString stringWithFormat:@"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\"><dict>"
		"<key>CFBundleIdentifier</key><string>%s</string><key>CFBundleExecutable</key><string>halo_replay</string>"
		"<key>CFBundlePackageType</key><string>APPL</string></dict></plist>\n", identifier];
	if (![files createDirectoryAtPath:macos withIntermediateDirectories:YES attributes:nil error:nil] ||
		![plist writeToFile:[app stringByAppendingPathComponent:@"Contents/Info.plist"] atomically:NO
			encoding:NSUTF8StringEncoding error:nil] ||
		![files copyItemAtPath:@(self) toPath:copy error:nil] ||
		![files copyItemAtPath:@(source_library) toPath:[macos stringByAppendingPathComponent:@"libSDL3.0.dylib"]
			error:nil])
	{
		fprintf(stderr, "halo_replay: cannot make %s\n", app.UTF8String);
		return 2;
	}
	/* (the shaders built ahead of time, as the game's app has them) */
	if (slash)
		snprintf(slash + 1, sizeof(source_library) - (size_t)(slash + 1 - source_library), "shaders.metallib");
	if (slash && [files fileExistsAtPath:@(source_library)])
	{
		NSString *resources = [app stringByAppendingPathComponent:@"Contents/Resources"];

		[files createDirectoryAtPath:resources withIntermediateDirectories:YES attributes:nil error:nil];
		[files copyItemAtPath:@(source_library) toPath:[resources stringByAppendingPathComponent:@"shaders.metallib"]
			error:nil];
	}
	arguments[count++] = (char *)copy.UTF8String;
	for (index = 1; index < argc; index++)
	{
		if (!strcmp(argv[index], "--cold"))
			continue;
		if (!strcmp(argv[index], "--archive"))
			has_archive = 1;
		arguments[count++] = argv[index];
	}
	if (!has_archive)
	{
		arguments[count++] = "--archive";
		arguments[count++] = (char *)archive.UTF8String;
	}
	printf("cold: in %s (bundle id %s: a new Metal cache; %s pipeline archive)\n", app.UTF8String, identifier,
		has_archive ? "the given" : "an empty");
	fflush(stdout);
	child = fork();
	if (child == 0)
	{
		execv(arguments[0], arguments);
		_exit(127);
	}
	if (child > 0 && waitpid(child, &status, 0) == child)
		status = WIFEXITED(status) ? WEXITSTATUS(status) : 2;
	[files removeItemAtPath:app error:nil];
	/* (the user's cache folder: confstr's, the folder Metal's caches are in) */
	{
		char directory[4096];

		if (confstr(_CS_DARWIN_USER_CACHE_DIR, directory, sizeof(directory)))
		{
			snprintf(cache, sizeof(cache), "%s/%s", directory, identifier);
			[files removeItemAtPath:@(cache) error:nil];
		}
	}
	return status;
}

/* ---------- main */

static void usage(void)
{
	fprintf(stderr, "usage: halo_replay <file> [--frames a-b] [--window] [--unpaced] [--shots dir] [--shot-frames list]\n"
		"\t[--loop N] [--cold] [--archive path] [--hitch ms] [--quiet] [--verbose]\n"
		"       halo_replay --compare <shots> <shots>\n");
}

int main(int argc, char *argv[])
{
	const char *path = NULL, *archive = NULL, *frames = NULL;
	char archive_default[4200], shot_default[64];
	int index, cold = 0;
	double started;

	if (getenv("HALO_ASTC_TEXTURES") && *getenv("HALO_ASTC_TEXTURES"))
		snprintf(host_astc_textures, sizeof(host_astc_textures), "%s", getenv("HALO_ASTC_TEXTURES"));
	if (argc == 4 && !strcmp(argv[1], "--compare"))
	{
		@autoreleasepool
		{
			return compare(argv[2], argv[3]);
		}
	}
	play.loops = 1;
	/* (1.5 refreshes at 120 Hz: a frame that misses one) */
	play.hitch_ms = 12.5;
	for (index = 1; index < argc; index++)
	{
		if (!strcmp(argv[index], "--frames") && index + 1 < argc)
			frames = argv[++index];
		else if (!strcmp(argv[index], "--window"))
			play.window = 1;
		else if (!strcmp(argv[index], "--unpaced"))
			play.unpaced = 1;
		else if (!strcmp(argv[index], "--shots") && index + 1 < argc)
			play.shots = argv[++index];
		else if (!strcmp(argv[index], "--shot-frames") && index + 1 < argc)
			play.shot_frames = argv[++index];
		else if (!strcmp(argv[index], "--loop") && index + 1 < argc)
			play.loops = (uint32_t)atoi(argv[++index]);
		else if (!strcmp(argv[index], "--cold"))
			cold = 1;
		else if (!strcmp(argv[index], "--archive") && index + 1 < argc)
			archive = argv[++index];
		else if (!strcmp(argv[index], "--hitch") && index + 1 < argc)
			play.hitch_ms = atof(argv[++index]);
		else if (!strcmp(argv[index], "--quiet"))
			play.quiet = 1;
		else if (!strcmp(argv[index], "--verbose"))
			verbose = 1;
		else if (argv[index][0] != '-' && !path)
			path = argv[index];
		else
		{
			usage();
			return 2;
		}
	}
	if (!path || play.loops < 1)
	{
		usage();
		return 2;
	}
	if (cold)
	{
		@autoreleasepool
		{
			return cold_run(argc, argv);
		}
	}
	@autoreleasepool
	{
		started = seconds_now();
		if (!recording_load(path) || !recording.frames)
		{
			fprintf(stderr, "halo_replay: no frames in %s\n", path);
			return 2;
		}
		printf("%s: %u records, %u frames, %u chunks, %.1f MB of contents (read in %.2f s)\n", path,
			recording.record_count, recording.frames, recording.chunk_count, recording.content_bytes / 1048576.0,
			seconds_now() - started);
		play.first_frame = 0;
		play.last_frame = recording.frames - 1;
		if (frames)
		{
			char *end;

			play.first_frame = (uint32_t)strtoul(frames, &end, 10);
			play.last_frame = *end == '-' ? (uint32_t)strtoul(end + 1, NULL, 10) : play.first_frame;
		}
		if (play.last_frame >= recording.frames)
			play.last_frame = recording.frames - 1;
		if (play.first_frame > play.last_frame)
		{
			fprintf(stderr, "halo_replay: no frames %s (the recording has %u)\n", frames, recording.frames);
			return 2;
		}
		/* (the records to the last frame's end) */
		{
			uint32_t ended = 0;

			for (play.play_count = 0; play.play_count < recording.record_count && ended <= play.last_frame;
				play.play_count++)
				ended += recording.records[play.play_count].type == XGPU_STREAM_FRAME_END;
		}
		if (play.shots && !play.shot_frames)
		{
			snprintf(shot_default, sizeof(shot_default), "%u-%u", play.first_frame, play.last_frame);
			play.shot_frames = shot_default;
		}
		play.frames = calloc((size_t)play.loops * (play.last_frame - play.first_frame + 1), sizeof(*play.frames));
		{
			const struct record *last = &recording.records[play.play_count - 1];
			uint64_t span = (uint32_t)(last->position + last->size - recording.records[0].position);
			uint64_t ring_size = recording.header.ring.size;

			play.shift = (span + ring_size - 1) / ring_size * ring_size;
		}

		if (!memory_make())
		{
			fprintf(stderr, "halo_replay: cannot reserve the guest's memory\n");
			return 2;
		}
		if (!archive)
		{
			snprintf(archive_default, sizeof(archive_default), "%s.pipelines", path);
			archive = archive_default;
		}
		setenv("HALO_METAL_ARCHIVE", archive, 1);
		if (play.window)
		{
			int width = 1280, height = 720;

			for (index = 0; index < (int)play.play_count; index++)
			{
				/* (the size of the first target presented: the back buffer's) */
				if (recording.records[index].type == XGPU_STREAM_TARGET_CREATE)
				{
					const struct xgpu_stream_target_create *create = (const void *)recording.records[index].data;

					if (!create->depth && create->width >= 320)
					{
						width = (int)create->width;
						height = (int)create->height;
						break;
					}
				}
			}
			if (!SDL_Init(SDL_INIT_VIDEO) ||
				!(window = SDL_CreateWindow("halo_replay", width / 2 > 640 ? width / 2 : width,
					width / 2 > 640 ? height / 2 : height, SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY)) ||
				!host_metal_window_open(1))
			{
				fprintf(stderr, "halo_replay: no window (%s)\n", SDL_GetError());
				return 2;
			}
		}
		else if (!host_metal_initialize())
		{
			fprintf(stderr, "halo_replay: no Metal device\n");
			return 2;
		}
		host_render_record_set_restorer(restore);
		/* (the Screenshot key's pictures are the game's, not the replay's) */
		host_render_stream_set_screenshot_writer(NULL);

		/* the ring's header, as it was when the recording began */
		ring = host_pointer(ring_address());
		pages_mark(ring_address(), sizeof(*ring), 0);
		*ring = recording.header.ring;
		ring->committed = ring->released = written = recording.records[0].position;
		ring->error = 0;
		if (host_render_stream_open(ring_address()))
		{
			fprintf(stderr, "halo_replay: the stream did not open\n");
			return 2;
		}
		started = seconds_now();
		{
			/* (the guest's side on a thread of its own: the main thread
			keeps SDL's events going for the window) */
			__block _Atomic int done = 0;
			NSThread *thread = [[NSThread alloc] initWithBlock:^{
				play_all();
				atomic_store(&done, 1);
			}];

			[thread start];
			while (!atomic_load(&done))
			{
				if (play.window)
				{
					SDL_Event event;

					while (SDL_PollEvent(&event))
						;
				}
				usleep(2000);
			}
		}
		host_render_stream_close();
		if (play.frame_closing)
		{
			play.frame_closing = 0;
			frame_closed();
		}
		{
			double seconds = seconds_now() - started;
			uint32_t loop;

			for (loop = 0; loop < play.loops; loop++)
				report(loop, seconds);
			printf("replayed %u records x %u in %.2f s; %.1f MB of memory put back; %u records out of place\n",
				play.play_count, play.loops, seconds, play.restored_bytes / 1048576.0, play.mismatches);
		}
		xgpu_metal_shutdown();
	}
	return 0;
}
