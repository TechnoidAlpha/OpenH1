/*
HOST_RENDER_RECORD.C

The command stream recorded as the render thread plays it, and the frames'
pictures taken (host_render_record.h). Everything happens on the render
thread, between the decoder's records, but the file's writing: the events
fill blocks, which a thread of the recording's own compresses (on macOS:
LZ4, Apple's libcompression; elsewhere they are stored as they are) and
writes, while the render thread fills the next. A content is hashed where
it is named.
*/

#include "host.h"
#include "host_render_record.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __APPLE__
#include <compression.h>
#endif

#define BLOCK_SIZE HOST_RENDER_RECORD_BLOCK
#define SHOT_RANGES 64

struct shot_range
{
	uint32_t first, last, step;
};

int host_render_record_active;

/* the blocks: the one filled (current) and the one written */
static struct
{
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t ready, freed;
	FILE *file;
	unsigned char *buffers[2], *compressed, *scratch;
	uint32_t used[2];
	int full[2];
	int current, next, stopping, failed;
} writer =
{
	.lock = PTHREAD_MUTEX_INITIALIZER,
	.ready = PTHREAD_COND_INITIALIZER,
	.freed = PTHREAD_COND_INITIALIZER,
};

static struct
{
	/* recording (the file is the writer's) */
	int file;
	/* the contents written, by hash (an open table; 0 is empty) */
	uint64_t *contents;
	uint32_t content_mask, content_count;
	uint32_t ring_buffer, ring_size;
	/* the record being played */
	uint32_t position, type, size;
	const unsigned char *record;
	uint32_t frame;
	host_render_shooter shooter;
	host_render_restorer restorer;
	void (*played)(void);
	char shots[1024];
	struct shot_range ranges[SHOT_RANGES];
	int range_count;
	uint64_t bytes, content_bytes, records, chunks;
} recorder;

static void active_update(void)
{
	host_render_record_active = recorder.file || recorder.restorer || recorder.range_count;
}

/* ---------- contents */

static uint64_t load64(const unsigned char *bytes)
{
	uint64_t value;

	memcpy(&value, bytes, 8);
	return value;
}

static uint64_t mix(uint64_t value)
{
	value ^= value >> 33;
	value *= 0xff51afd7ed558ccdull;
	value ^= value >> 33;
	value *= 0xc4ceb9fe1a85ec53ull;
	value ^= value >> 33;
	return value;
}

uint64_t host_render_record_hash(const void *data, uint32_t size)
{
	const unsigned char *bytes = data;
	const uint64_t multiplier = 0x9e3779b97f4a7c15ull;
	uint64_t lanes[4] = { size ^ 0x243f6a8885a308d3ull, 0x13198a2e03707344ull, 0xa4093822299f31d0ull,
		0x082efa98ec4e6c89ull };
	uint64_t hash;
	uint32_t left = size;
	int lane;

	for (; left >= 32; left -= 32, bytes += 32)
	{
		for (lane = 0; lane < 4; lane++)
		{
			lanes[lane] = (lanes[lane] ^ load64(bytes + lane * 8)) * multiplier;
			lanes[lane] ^= lanes[lane] >> 29;
		}
	}
	for (; left >= 8; left -= 8, bytes += 8)
		lanes[0] = mix(lanes[0] ^ load64(bytes));
	if (left)
	{
		unsigned char tail[8] = { 0 };

		memcpy(tail, bytes, left);
		lanes[1] = mix(lanes[1] ^ load64(tail) ^ left);
	}
	hash = mix(lanes[0]) ^ mix(lanes[1] + 1) ^ mix(lanes[2] + 2) ^ mix(lanes[3] + 3);
	return hash ? hash : 1;
}

/* nonzero if the hash was not yet in the table (and is now) */
static int content_add(uint64_t hash)
{
	uint32_t index;

	if ((recorder.content_count + 1) * 2 > recorder.content_mask + 1)
	{
		uint32_t size = (recorder.content_mask + 1) * 2, old, at;
		uint64_t *grown = calloc(size, sizeof(*grown));

		if (!grown)
			return 1;
		for (old = 0; recorder.contents && old <= recorder.content_mask; old++)
		{
			if (!recorder.contents[old])
				continue;
			for (at = (uint32_t)recorder.contents[old] & (size - 1); grown[at]; at = (at + 1) & (size - 1))
				;
			grown[at] = recorder.contents[old];
		}
		free(recorder.contents);
		recorder.contents = grown;
		recorder.content_mask = size - 1;
	}
	for (index = (uint32_t)hash & recorder.content_mask; recorder.contents[index];
		index = (index + 1) & recorder.content_mask)
	{
		if (recorder.contents[index] == hash)
			return 0;
	}
	recorder.contents[index] = hash;
	recorder.content_count++;
	return 1;
}

/* ---------- the file */

/* (the writer's thread) a block, compressed if that makes it smaller */
static int block_write(const unsigned char *data, uint32_t size)
{
	struct host_render_record_block block;
	const unsigned char *stored = data;

	block.size = size;
	block.stored = size;
#ifdef __APPLE__
	if (writer.compressed && writer.scratch)
	{
		size_t written = compression_encode_buffer(writer.compressed, BLOCK_SIZE, data, size, writer.scratch,
			COMPRESSION_LZ4);

		if (written && written < size)
		{
			block.stored = (uint32_t)written;
			stored = writer.compressed;
		}
	}
#endif
	return fwrite(&block, sizeof(block), 1, writer.file) == 1 && fwrite(stored, block.stored, 1, writer.file) == 1;
}

static void *writer_thread(void *argument)
{
	(void)argument;
	pthread_mutex_lock(&writer.lock);
	for (;;)
	{
		int index = writer.next;

		if (!writer.full[index])
		{
			if (writer.stopping)
				break;
			pthread_cond_wait(&writer.ready, &writer.lock);
			continue;
		}
		pthread_mutex_unlock(&writer.lock);
		if (!writer.failed && !block_write(writer.buffers[index], writer.used[index]))
		{
			host_logf(HOST_LOG_ERROR, "render record: cannot write the file (%s): the recording ends", strerror(errno));
			writer.failed = 1;
		}
		pthread_mutex_lock(&writer.lock);
		writer.used[index] = 0;
		writer.full[index] = 0;
		writer.next = !index;
		pthread_cond_broadcast(&writer.freed);
	}
	pthread_mutex_unlock(&writer.lock);
	return NULL;
}

/* the current block to the writer; waits for the other to be written */
static void block_submit(void)
{
	pthread_mutex_lock(&writer.lock);
	writer.full[writer.current] = 1;
	pthread_cond_signal(&writer.ready);
	writer.current = !writer.current;
	while (writer.full[writer.current])
		pthread_cond_wait(&writer.freed, &writer.lock);
	pthread_mutex_unlock(&writer.lock);
}

static void append(const void *data, uint32_t size)
{
	const unsigned char *from = data;

	while (size)
	{
		uint32_t used = writer.used[writer.current], room = BLOCK_SIZE - used, part = size < room ? size : room;

		memcpy(writer.buffers[writer.current] + used, from, part);
		writer.used[writer.current] = used + part;
		from += part;
		size -= part;
		if (writer.used[writer.current] == BLOCK_SIZE)
			block_submit();
	}
}

static void event_write(uint32_t type, const void *first, uint32_t first_size, const void *second, uint32_t second_size)
{
	struct host_render_record_event event;

	if (writer.failed)
	{
		recorder.file = 0;
		active_update();
		return;
	}
	event.type = type;
	event.size = first_size + second_size;
	append(&event, sizeof(event));
	append(first, first_size);
	append(second, second_size);
	recorder.bytes += sizeof(event) + event.size;
}

static int writer_start(FILE *file)
{
	writer.file = file;
	writer.buffers[0] = malloc(BLOCK_SIZE);
	writer.buffers[1] = malloc(BLOCK_SIZE);
#ifdef __APPLE__
	writer.compressed = malloc(BLOCK_SIZE);
	writer.scratch = malloc(compression_encode_scratch_buffer_size(COMPRESSION_LZ4));
#endif
	writer.used[0] = writer.used[1] = 0;
	writer.full[0] = writer.full[1] = 0;
	writer.current = writer.next = 0;
	writer.stopping = writer.failed = 0;
	return writer.buffers[0] && writer.buffers[1] && !pthread_create(&writer.thread, NULL, writer_thread, NULL);
}

static void writer_stop(void)
{
	if (writer.used[writer.current])
	{
		pthread_mutex_lock(&writer.lock);
		writer.full[writer.current] = 1;
		pthread_mutex_unlock(&writer.lock);
	}
	pthread_mutex_lock(&writer.lock);
	writer.stopping = 1;
	pthread_cond_signal(&writer.ready);
	pthread_mutex_unlock(&writer.lock);
	pthread_join(writer.thread, NULL);
	if (fclose(writer.file) || writer.failed)
		host_logf(HOST_LOG_ERROR, "render record: the file is not whole (%s)", strerror(errno));
	free(writer.buffers[0]);
	free(writer.buffers[1]);
	free(writer.compressed);
	free(writer.scratch);
	memset(writer.buffers, 0, sizeof(writer.buffers));
	writer.compressed = writer.scratch = NULL;
}

void host_render_record_note(uint32_t address, uint32_t size)
{
	struct host_render_record_chunk chunk;
	const void *data;
	uint64_t hash;

	if (!recorder.file || !address || !size)
		return;
	data = host_pointer(address);
	hash = host_render_record_hash(data, size);
	if (content_add(hash))
	{
		event_write(HOST_RENDER_EVENT_CONTENT, &hash, sizeof(hash), data, size);
		recorder.content_bytes += size;
	}
	chunk.address = address;
	chunk.size = size;
	chunk.hash[0] = (uint32_t)hash;
	chunk.hash[1] = (uint32_t)(hash >> 32);
	event_write(HOST_RENDER_EVENT_CHUNK, &chunk, sizeof(chunk), NULL, 0);
	recorder.chunks++;
}

/* inline data outside the ring (in a buffer of its own) */
static void outside(uint32_t address, uint32_t bytes)
{
	if (address && bytes && address - recorder.ring_buffer >= recorder.ring_size)
		host_render_record_note(address, bytes);
}

/* the guest memory a record names, outside the ring (the decoder checks
the records' sizes after: here they are checked before they are read) */
static void record_memory(const unsigned char *start, uint32_t type, uint32_t size)
{
	switch (type)
	{
	case XGPU_STREAM_MIRROR_WRITE:
	{
		const struct xgpu_stream_mirror_write *write = (const void *)start;

		outside(write->data, write->size);
		break;
	}
	case XGPU_STREAM_VERTICES:
	{
		const struct xgpu_stream_vertices *vertices = (const void *)start;
		const struct xgpu_stream_vertex_stream *streams = (const void *)(vertices + 1);
		uint32_t count = (uint32_t)__builtin_popcount(vertices->streams), index;

		if (sizeof(*vertices) + count * sizeof(*streams) > size)
			break;
		for (index = 0; index < count; index++)
		{
			if (streams[index].segment < 0)
				outside(streams[index].data, streams[index].bytes);
		}
		break;
	}
	case XGPU_STREAM_DRAW_INDICES:
	{
		const struct xgpu_stream_draw_indices *draw = (const void *)start;

		if (draw->count <= 0x7fffffffu)
			outside(draw->indices, draw->count * 2);
		break;
	}
	case XGPU_STREAM_DRAW_INDEXED:
	{
		const struct xgpu_stream_draw_indexed *draw = (const void *)start;

		if (draw->segment < 0 && draw->count <= 0x7fffffffu)
			outside(draw->indices, draw->count * 2);
		break;
	}
	case XGPU_STREAM_DRAW_IMMEDIATE:
	{
		const struct xgpu_stream_draw_immediate *draw = (const void *)start;
		uint64_t vertex_bytes = (uint64_t)draw->count * (uint32_t)__builtin_popcount(draw->written) * 16;

		if (vertex_bytes <= 0xffffffffu)
			outside(draw->vertices, (uint32_t)vertex_bytes);
		if (draw->index_count <= 0x7fffffffu)
			outside(draw->indices, draw->index_count * 2);
		break;
	}
	case XGPU_STREAM_DYNAMIC_LIGHTS_UPLOAD:
	{
		const struct xgpu_stream_dynamic_lights_upload *upload = (const void *)start;

		outside(upload->cells, upload->size);
		break;
	}
	case XGPU_STREAM_DYNAMIC_LIGHTS_CHANGE:
	{
		const struct xgpu_stream_dynamic_lights_change *change = (const void *)start;

		if (change->size)
			outside(change->cells, change->size);
		break;
	}
	case XGPU_STREAM_DYNAMIC_LIGHTS_WORLD:
	{
		const struct xgpu_stream_dynamic_lights_world *world = (const void *)start;

		if (world->count <= 0xfffffffu)
			outside(world->texels, world->count * 16);
		break;
	}
	case XGPU_STREAM_VR_INITIALIZE:
		outside(((const struct xgpu_stream_reply_record *)start)->reply, sizeof(struct xgpu_stream_vr_info));
		break;
	default:
		break;
	}
}

/* ---------- shots */

static void shots_parse(const char *frames)
{
	const char *place = frames;

	recorder.range_count = 0;
	while (place && *place && recorder.range_count < SHOT_RANGES)
	{
		struct shot_range range;
		char *end;

		range.first = (uint32_t)strtoul(place, &end, 10);
		if (end == place)
			break;
		range.last = range.first;
		range.step = 1;
		place = end;
		if (*place == '-')
		{
			range.last = (uint32_t)strtoul(place + 1, &end, 10);
			place = end;
		}
		if (*place == '/')
		{
			range.step = (uint32_t)strtoul(place + 1, &end, 10);
			place = end;
			if (!range.step)
				range.step = 1;
		}
		recorder.ranges[recorder.range_count++] = range;
		if (*place == ',')
			place++;
	}
}

int host_render_record_shot_wanted(uint32_t frame)
{
	int index;

	for (index = 0; index < recorder.range_count; index++)
	{
		const struct shot_range *range = &recorder.ranges[index];

		if (frame >= range->first && frame <= range->last && (frame - range->first) % range->step == 0)
			return 1;
	}
	return 0;
}

void host_render_record_shots(const char *directory, const char *frames)
{
	recorder.range_count = 0;
	if (directory && *directory && frames && *frames)
	{
		snprintf(recorder.shots, sizeof(recorder.shots), "%s", directory);
		mkdir(directory, 0755);
		shots_parse(frames);
	}
	active_update();
}

void host_render_record_set_shooter(host_render_shooter shooter)
{
	recorder.shooter = shooter;
}

void host_render_record_set_restorer(host_render_restorer restorer)
{
	recorder.restorer = restorer;
	active_update();
}

void host_render_record_set_played(void (*played)(void))
{
	recorder.played = played;
}

uint32_t host_render_record_frame(void)
{
	return recorder.frame;
}

void host_render_record_set_frame(uint32_t frame)
{
	recorder.frame = frame;
}

/* ---------- the decoder's */

void host_render_record_before(const struct xgpu_stream_ring *ring, uint32_t position, const unsigned char *record,
	uint32_t type, uint32_t size)
{
	(void)ring;
	if (recorder.restorer)
		recorder.restorer(position, type, record);
	if (type == XGPU_STREAM_PRESENT && recorder.range_count && recorder.shooter && host_render_record_shot_wanted(recorder.frame))
	{
		char path[1100];

		snprintf(path, sizeof(path), "%s/frame_%05u.png", recorder.shots, recorder.frame);
		if (!recorder.shooter(((const struct xgpu_stream_value *)record)->value, path))
			host_logf(HOST_LOG_WARN, "render record: frame %u's picture not taken", recorder.frame);
	}
	recorder.position = position;
	recorder.type = type;
	recorder.size = size;
	recorder.record = record;
	if (recorder.file)
		record_memory(record, type, size);
}

void host_render_record_after(void)
{
	if (recorder.file)
	{
		/* (a SKIP's bytes are not read: its header alone) */
		uint32_t bytes = recorder.type == XGPU_STREAM_SKIP ? sizeof(uint32_t) : recorder.size;

		event_write(HOST_RENDER_EVENT_RECORD, &recorder.position, sizeof(recorder.position), recorder.record, bytes);
		recorder.records++;
	}
	if (recorder.played)
		recorder.played();
	if (recorder.type == XGPU_STREAM_FRAME_END)
		recorder.frame++;
}

/* ---------- the stream's life */

void host_render_record_start(const struct xgpu_stream_ring *ring)
{
	const char *path = getenv("HALO_RENDER_RECORD");
	struct host_render_record_file header;

	recorder.frame = 0;
	recorder.ring_buffer = ring->buffer;
	recorder.ring_size = ring->size;
	if (!recorder.range_count)
		host_render_record_shots(getenv("HALO_RENDER_SHOTS"), getenv("HALO_RENDER_SHOT_FRAMES"));
	if (path && *path && !recorder.file)
	{
		FILE *file = fopen(path, "wb");

		if (!file)
		{
			host_logf(HOST_LOG_ERROR, "render record: cannot make %s (%s)", path, strerror(errno));
		}
		else
		{
			memset(&header, 0, sizeof(header));
			header.magic = HOST_RENDER_RECORD_MAGIC;
			header.version = HOST_RENDER_RECORD_VERSION;
			header.header_size = sizeof(header);
			header.page_size = (uint32_t)sysconf(_SC_PAGESIZE);
			header.ring_address = guest_address(ring);
			header.ring = *ring;
			header.block_size = BLOCK_SIZE;
			recorder.bytes = sizeof(header);
			if (fwrite(&header, sizeof(header), 1, file) != 1 || !writer_start(file))
			{
				host_logf(HOST_LOG_ERROR, "render record: cannot write %s (%s)", path, strerror(errno));
				fclose(file);
			}
			else
			{
				recorder.file = 1;
				host_logf(HOST_LOG_INFO, "render record: recording to %s", path);
			}
		}
	}
	active_update();
}

void host_render_record_stop(void)
{
	if (writer.file)
	{
		host_logf(HOST_LOG_INFO,
			"render record: %u frames, %llu records, %llu chunks, %u contents (%.1f MB), %.1f MB written", recorder.frame,
			(unsigned long long)recorder.records, (unsigned long long)recorder.chunks, recorder.content_count,
			recorder.content_bytes / 1048576.0, recorder.bytes / 1048576.0);
		writer_stop();
		writer.file = NULL;
		recorder.file = 0;
	}
	active_update();
}
