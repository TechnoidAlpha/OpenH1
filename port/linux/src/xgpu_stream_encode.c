/*
XGPU_STREAM_ENCODE.C

The renderer's backend (xgpu_backend.h) as a command stream
(xgpu_stream.h): each call is written as a record into a ring in the
game's memory, and a backend on the 64-bit host plays the frame on a
thread of its own (port/android/host/host_render_stream.c), while the game
goes on to the next. One host call a frame hands the frame over
(host_render_stream_submit); the game waits only when the ring is full or
two frames are still unfinished.

Every build's backend: the host's is Vulkan's (port/linux/vulkan) or
Metal's (port/macos). The calls that answer at once are answered here: the
capabilities and the VR mode's at start-up, by a synchronous record; a
pipeline's success a frame or two late (TRUE until the host says
otherwise); visibility counts from where the host writes them; a texture's
description from its header; a target's pixels (screenshots)
synchronously.

Records are written compactly: pipeline keys and vertex layouts are sent
once and named by a number after, and the draw state, textures and the
inputs no stream feeds only when they change.

Every float the game hands the GPU passes here (its vertex shaders'
constants, the combiners' and fog's, immediate mode's vertices, the
enhanced dynamic lights and their glows): one that is not a number, or is
infinite, goes as 0 (stream_finite), said once in a while with what it was
and where. The Xbox's NV2A computed none, and a GPU draws one as anything:
black, white or blocks.
*/

#include "frame_pacing.h"
#include "xgpu_backend.h"
#include "xgpu_images.h"
#include "xgpu_shader.h"
#include "xgpu_stream.h"
#include "xgpu_stream_describe.h"
#ifndef XGPU_STREAM_TEST
#include "sdl_platform.h"
#endif
#ifdef HALO_VR
#include "vr.h"
#include "vr_visor.h"

#include <stddef.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void platform_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* port/android/guest/runtime/guest_host.h */
int host_render_stream_open(unsigned int ring);
void host_render_stream_submit(unsigned int flags);
void host_render_stream_wait(unsigned int position);
void host_render_stream_close(void);

void xgpu_stream_encode_close(void);

/* the guest address of guest memory; guest memory at one of the
interface's addresses (DWORD, unsigned long); and at one of the stream's
(32 bits: the same in the guest, from the test's window's base in its
test) */
#define STREAM_ADDRESS(pointer) ((uint32_t)(uintptr_t)(pointer))
#define INTERFACE_POINTER(address) ((const void *)(uintptr_t)(address))
#ifdef XGPU_STREAM_TEST
extern uintptr_t host_guest_base;
#define STREAM_POINTER(address) ((const void *)(host_guest_base + (uint32_t)(address)))
#else
#define STREAM_POINTER(address) ((const void *)(uintptr_t)(address))
#endif

#ifdef XGPU_STREAM_TEST
/* (host_render_stream_test.c: memory in the test's guest window) */
void *xgpu_stream_test_allocate(size_t alignment, size_t size);
#define stream_allocate xgpu_stream_test_allocate
#define stream_free(pointer) ((void)(pointer))
#else
#define stream_allocate aligned_alloc
#define stream_free free
#endif

/* the ring: 32 MB (a busy frame writes a few, most of it vertices), two
frames in flight */
#define RING_SIZE_DEFAULT (32u << 20)
#define FRAMES_IN_FLIGHT_DEFAULT 2u
#define TARGET_MAXIMUM 4096
#define ASIDE_MAXIMUM 64
/* a map's preparation's records handed to the render thread this often
(preparation_commit) */
#define PREPARATION_SUBMIT (64u << 10)

/* the interface's pieces the format copies as their bytes */
_Static_assert(sizeof(struct xgpu_vertex_instruction) == XGPU_STREAM_VERTEX_INSTRUCTION_SIZE, "vertex instruction");
_Static_assert(sizeof(struct xgpu_texture_stage) == XGPU_STREAM_TEXTURE_STAGE_SIZE, "texture stage");
_Static_assert(sizeof(struct xgpu_combiner_stage) == XGPU_STREAM_COMBINER_STAGE_SIZE, "combiner stage");
_Static_assert(sizeof(struct xgpu_final_combiner) == XGPU_STREAM_FINAL_COMBINER_SIZE, "final combiner");
_Static_assert(sizeof(struct xgpu_vertex_element) == sizeof(struct xgpu_stream_vertex_element), "vertex element");
_Static_assert(sizeof(struct xgpu_draw_uniforms) == sizeof(struct xgpu_stream_draw_uniforms), "draw uniforms");
_Static_assert(sizeof(struct xgpu_dynamic_lights_block) == sizeof(struct xgpu_stream_dynamic_lights_block),
	"dynamic lights block");
_Static_assert(XGPU_DYNAMIC_LIGHTS_MAXIMUM == XGPU_STREAM_DYNAMIC_LIGHTS_MAXIMUM, "dynamic lights");
_Static_assert(XGPU_MIP_LEVELS == XGPU_STREAM_MIP_LEVELS, "mip levels");
_Static_assert(XGPU_VERTEX_PROGRAM_MAXIMUM == XGPU_STREAM_VERTEX_PROGRAM_MAXIMUM, "vertex program");
_Static_assert(XGPU_VISIBILITY_TEST_SLOTS == XGPU_STREAM_VISIBILITY_SLOTS, "visibility slots");
_Static_assert(D3DRS_PS_MAX == XGPU_STREAM_COMBINER_STATES, "combiner states");
_Static_assert(sizeof(struct xgpu_pipeline_key) % 4 == 0, "pipeline key");
_Static_assert(XGPU_STREAM_UNIFORM_PIECES <= 64, "uniform pieces");

struct xgpu_backend_capabilities xgpu_backend_capabilities;

/* the numbered definitions (pipeline keys, vertex layouts): their bytes
by number, found by a hash of them */
struct numbering
{
	uint32_t size;      /* of a definition */
	uint32_t limit;     /* the numbers: 1 to limit - 1 */
	uint32_t next;
	uint32_t slot_mask;
	uint16_t *slots;    /* numbers, 0 empty */
	unsigned char *definitions;
	uint32_t last;      /* the number found last */
	uint32_t rounds;    /* the times the numbers have begun again */
};

static struct
{
	BOOL open;
	/* a map's preparation is being written (xgpu_backend_prepare to
	xgpu_backend_prepared): preparation_commit */
	BOOL preparing;
	struct xgpu_stream_ring *ring;
	unsigned char *buffer;
	uint32_t size, mask;
	uint32_t inline_limit;
	/* where the next record goes; where the frame began */
	uint32_t position;
	uint32_t frame_start;
	/* the copies of data too large for the ring, each freed once the
	host has released its record (released at least release) */
	struct aside
	{
		void *data;
		uint32_t release;
		/* a VERTICES record's: the host's vertex input reads it until
		another VERTICES record replaces it (release then set) */
		BOOL pinned;
	} asides[ASIDE_MAXIMUM];
	uint32_t aside_count;
	/* submits so far: the host releases a submit's bytes all at once,
	when the GPU is done with the work of all its records */
	uint32_t batch;
	/* the last VERTICES record whose streamed data the host's vertex
	input reads (inline in the ring, or aside), so that a draw in a later
	submit than it can send it again (its bytes may be released before
	that draw is played): its submit, its size, and what it holds */
	struct
	{
		BOOL streamed, inline_data;
		uint32_t batch, position, fixed, size;
		uint32_t layout, present;
		struct xgpu_stream_vertex_stream streams[16];
	} vertices;
	volatile uint32_t *visibility;
	volatile uint8_t *pipelines;

	uint32_t ring_size, frames_in_flight;
	struct xgpu_stream_capabilities capabilities;
	/* the synchronous records' answers, where the host writes them */
	struct replies
	{
		struct xgpu_stream_capabilities capabilities;
		uint32_t value;
		struct xgpu_stream_vr_info vr_info;
	} *replies;

	/* what the host last had */
	uint32_t pipeline;
	BOOL dynamic_sent, uniforms_sent;
	struct xgpu_stream_dynamic_state dynamic;
	uint32_t uniforms_serial;
	struct xgpu_stream_draw_uniforms uniforms;
	/* the last key's, and its number */
	struct xgpu_pipeline_key last_key;
	BOOL stages_sent[4];
	struct xgpu_stream_texture_stage stages[4];
	struct xgpu_stream_texture_level levels[4][XGPU_STREAM_MIP_LEVELS];
	/* the values of the inputs no stream feeds (set_vertices'), and of
	the registers immediate mode's vertices do not write (each the host's
	own) */
	struct attributes
	{
		uint32_t sent;
		float values[XGPU_VERTEX_ATTRIBUTE_COUNT][4];
	} vertex_attributes, immediate_attributes;

	struct numbering pipeline_keys, layouts;
	struct xgpu_stream_pipeline_key key;
	unsigned char layout[sizeof(struct xgpu_stream_layout_define) + XGPU_VERTEX_ATTRIBUTE_COUNT *
		sizeof(struct xgpu_stream_vertex_element)];

	/* the targets' sizes, for their pixels (xgpu_backend_read_target) */
	struct
	{
		uint32_t width, height;
	} targets[TARGET_MAXIMUM];
} stream;

/* (tests and measurements) the ring's size and frames in flight, before
xgpu_backend_initialize; and the ring's header, for its statistics */
void xgpu_stream_encode_configure(unsigned long ring_size, unsigned long frames_in_flight)
{
	stream.ring_size = (uint32_t)ring_size;
	stream.frames_in_flight = (uint32_t)frames_in_flight;
}

const struct xgpu_stream_ring *xgpu_stream_encode_ring(void)
{
	return stream.ring;
}

static uint32_t align_up(uint32_t value, uint32_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

static uint32_t load_acquire(const uint32_t *value)
{
	return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

/* ---------- the ring */

/* what the next frame takes more than the frames do, and the frames it is
told with yet (xgpu_backend_frame_extra) */
static uint32_t frame_extra_microseconds, frame_extra_frames;

static void commit(unsigned int flags)
{
	struct xgpu_stream_ring *ring = stream.ring;

	if (flags & XGPU_STREAM_SUBMIT_FRAME)
	{
		uint32_t bytes = stream.position - stream.frame_start;

		ring->frame_bytes = bytes;
		if (bytes > ring->frame_bytes_most)
			ring->frame_bytes_most = bytes;
		stream.frame_start = stream.position;
		ring->frames_submitted++;
		ring->frame_extra_microseconds = frame_extra_frames ? frame_extra_microseconds : 0;
		if (frame_extra_frames)
			frame_extra_frames--;
	}
	__atomic_store_n(&ring->committed, stream.position, __ATOMIC_RELEASE);
	stream.batch++;
	host_render_stream_submit(flags);
}

static void vertices_keep(uint32_t size);

/* A map's preparation (render_enumerate.c) writes hundreds of megabytes of
records in one frame, and waits for the render thread to have played them
each time the texture cache makes room (xgpu_backend_consume, a few hundred
times). Handed over only then, they were played while the game waited, and
the render thread waited while the game wrote the next; handed over each
PREPARATION_SUBMIT bytes, they are played as the game writes more. Called
only where a submit may come (before a record that begins a draw's state). */
static void preparation_commit(void)
{
	if (stream.preparing && stream.position - stream.ring->committed >= PREPARATION_SUBMIT)
		commit(0);
}

/* waits until size bytes from position are free */
static void room(uint32_t size)
{
	struct xgpu_stream_ring *ring = stream.ring;

	vertices_keep(size);
	while (stream.position + size - load_acquire(&ring->released) > stream.size)
	{
		commit(0);
		host_render_stream_wait(stream.position + size - stream.size);
	}
}

/* a record of type: its fixed part's bytes and its inline data's, made
room for and its header written; its data's place (16-byte aligned) in
*data. Returns the fixed part, zeroed. */
static void *record_data(uint32_t type, uint32_t fixed, uint32_t data_size, unsigned char **data)
{
	for (;;)
	{
		uint32_t offset = stream.position & stream.mask;
		uint32_t data_offset = data_size ? align_up(offset + fixed, XGPU_STREAM_DATA_ALIGNMENT) - offset : fixed;
		uint32_t size = align_up(data_offset + data_size, XGPU_STREAM_RECORD_ALIGNMENT);
		unsigned char *start;

		if (offset + size > stream.size)
		{
			/* to the ring's end, then from its start */
			uint32_t skip = stream.size - offset;

			room(skip);
			*(uint32_t *)(stream.buffer + offset) = XGPU_STREAM_HEADER(XGPU_STREAM_SKIP, 0);
			stream.position += skip;
			continue;
		}
		room(size);
		start = stream.buffer + offset;
		memset(start, 0, fixed);
		*(uint32_t *)start = XGPU_STREAM_HEADER(type, size);
		if (data)
			*data = start + data_offset;
		stream.position += size;
		return start;
	}
}

static uint32_t record_size(const void *record_body)
{
	return XGPU_STREAM_HEADER_SIZE(*(const uint32_t *)record_body);
}

static void *record(uint32_t type, uint32_t fixed)
{
	return record_data(type, fixed, 0, NULL);
}

static void record_value(uint32_t type, uint32_t value)
{
	struct xgpu_stream_value *record_body = record(type, sizeof(*record_body));

	record_body->value = value;
}

/* the copies the host is done with, freed */
static void asides_free(void)
{
	uint32_t released = load_acquire(&stream.ring->released);
	uint32_t index = 0;

	while (index < stream.aside_count)
	{
		if (!stream.asides[index].pinned && (int32_t)(released - stream.asides[index].release) >= 0)
		{
			stream_free(stream.asides[index].data);
			stream.asides[index] = stream.asides[--stream.aside_count];
		}
		else
		{
			index++;
		}
	}
}

/* bytes of guest data a record carries: inline when they fit the limit
(the size to reserve), else copied aside (0 reserved) */
static uint32_t carried(uint32_t bytes)
{
	return bytes <= stream.inline_limit ? bytes : 0;
}

/* room for count more buffers aside, made before the record that names
them is begun (a record is never submitted half written) */
static void asides_reserve(uint32_t count)
{
	asides_free();
	/* (and the last VERTICES record's, which room may set aside:
	vertices_keep) */
	if (stream.aside_count + count + 16 > ASIDE_MAXIMUM)
	{
		/* (all still the host's: once it is done with them) */
		commit(XGPU_STREAM_SUBMIT_SYNC);
		asides_free();
	}
}

/* a buffer the record just begun names (room reserved), freed once the
host has released the record (pinned: once it has released the record
that replaces it); its address */
static uint32_t aside_pinned(void *copy, BOOL pinned)
{
	if (stream.aside_count == ASIDE_MAXIMUM)
	{
		/* (asides_reserve makes room first: never) */
		stream_free(copy);
		return 0;
	}
	stream.asides[stream.aside_count].data = copy;
	stream.asides[stream.aside_count].release = stream.position;
	stream.asides[stream.aside_count].pinned = pinned;
	stream.aside_count++;
	return STREAM_ADDRESS(copy);
}

static uint32_t aside(void *copy)
{
	return aside_pinned(copy, FALSE);
}

/* the pinned buffers freed once the host has released what is written so
far (a record that replaces them) */
static void asides_unpin(void)
{
	uint32_t index;

	for (index = 0; index < stream.aside_count; index++)
	{
		if (stream.asides[index].pinned)
		{
			stream.asides[index].pinned = FALSE;
			stream.asides[index].release = stream.position;
		}
	}
}

/* data's address in the record: copied to place (if there is one: room
for it was reserved), or to a buffer of its own (pinned: aside_pinned);
0 if there is no memory for that */
static uint32_t carry_pinned(unsigned char **place, const void *data, uint32_t bytes, BOOL pinned)
{
	uint32_t address;
	void *copy;

	if (!bytes)
		return 0;
	if (place)
	{
		memcpy(*place, data, bytes);
		address = STREAM_ADDRESS(*place);
		*place += align_up(bytes, XGPU_STREAM_DATA_ALIGNMENT);
		return address;
	}
	copy = stream_allocate(XGPU_STREAM_DATA_ALIGNMENT, align_up(bytes, XGPU_STREAM_DATA_ALIGNMENT));
	if (!copy)
		return 0;
	memcpy(copy, data, bytes);
	return aside_pinned(copy, pinned);
}

static uint32_t carry(unsigned char **place, const void *data, uint32_t bytes)
{
	return carry_pinned(place, data, bytes, FALSE);
}

/* carry, for data alone in its record (inline if it fits; asides_reserve
made for it if not) */
static uint32_t carry_one(unsigned char **place, const void *data, uint32_t bytes)
{
	return carry(bytes <= stream.inline_limit ? place : NULL, data, bytes);
}

/* carried, for data alone in its record */
static uint32_t carried_one(uint32_t bytes)
{
	if (bytes > stream.inline_limit)
	{
		asides_reserve(1);
		return 0;
	}
	return bytes;
}

/* a synchronous record's answer: the records so far played and done */
static void synchronous(void)
{
	commit(XGPU_STREAM_SUBMIT_SYNC);
}

/* what the host had is unknown to it now (or the stream began): sent
again as it is next used */
static void forget_sent(void)
{
	stream.pipeline = 0;
	stream.dynamic_sent = FALSE;
	stream.uniforms_sent = FALSE;
	memset(stream.stages_sent, 0, sizeof(stream.stages_sent));
	stream.vertex_attributes.sent = 0;
	stream.immediate_attributes.sent = 0;
}

/* ---------- numbered definitions */

static BOOL numbering_initialize(struct numbering *numbering, uint32_t size, uint32_t limit)
{
	numbering->size = size;
	numbering->limit = limit;
	numbering->next = 1;
	numbering->slot_mask = limit * 2 - 1;
	numbering->slots = calloc(limit * 2, sizeof(*numbering->slots));
	numbering->definitions = malloc((size_t)size * limit);
	numbering->last = 0;
	return numbering->slots && numbering->definitions;
}

/* (of 4-byte words: the definitions are). Four chains of words, each its own
multiplies, so that they run side by side: a pipeline key is some 400 bytes,
hashed for each draw whose pipeline is not the last one's */
static uint32_t hash_words(const void *data, uint32_t size)
{
	const unsigned char *bytes = data;
	uint32_t a = 2166136261u, b = 2166136261u + 1u, c = 2166136261u + 2u, d = 2166136261u + 3u;
	uint32_t index, word, hash;

	for (index = 0; index + 16 <= size; index += 16)
	{
		memcpy(&word, bytes + index, 4);
		a = (a ^ word) * 16777619u;
		memcpy(&word, bytes + index + 4, 4);
		b = (b ^ word) * 16777619u;
		memcpy(&word, bytes + index + 8, 4);
		c = (c ^ word) * 16777619u;
		memcpy(&word, bytes + index + 12, 4);
		d = (d ^ word) * 16777619u;
	}
	hash = a ^ ((b << 7) | (b >> 25)) ^ ((c << 13) | (c >> 19)) ^ ((d << 19) | (d >> 13));
	for (; index + 4 <= size; index += 4)
	{
		memcpy(&word, bytes + index, 4);
		hash = (hash ^ word) * 16777619u;
	}
	hash ^= hash >> 15;
	hash *= 0x85ebca6bu;
	hash ^= hash >> 13;
	return hash;
}

/* the definition's number; *made if it is new (then sent by the caller).
last_differs: the caller has found it is not the last one found */
static uint32_t numbering_find(struct numbering *numbering, const void *definition, BOOL *made, BOOL last_differs)
{
	uint32_t slot;
	uint32_t number;

	*made = FALSE;
	if (!last_differs && numbering->last &&
		!memcmp(numbering->definitions + (size_t)numbering->last * numbering->size, definition, numbering->size))
		return numbering->last;
	for (slot = hash_words(definition, numbering->size) & numbering->slot_mask;
		(number = numbering->slots[slot]) != 0; slot = (slot + 1) & numbering->slot_mask)
	{
		if (!memcmp(numbering->definitions + (size_t)number * numbering->size, definition, numbering->size))
			return numbering->last = number;
	}
	if (numbering->next == numbering->limit)
	{
		/* all numbered: they begin again (the host's are redefined as
		they come) */
		memset(numbering->slots, 0, sizeof(*numbering->slots) * (numbering->slot_mask + 1));
		numbering->next = 1;
		numbering->last = 0;
		numbering->rounds++;
		forget_sent();
		return numbering_find(numbering, definition, made, FALSE);
	}
	number = numbering->next++;
	numbering->slots[slot] = (uint16_t)number;
	memcpy(numbering->definitions + (size_t)number * numbering->size, definition, numbering->size);
	*made = TRUE;
	return numbering->last = number;
}

/* ---------- the backend's life */

void xgpu_backend_initialize(void)
{
	struct xgpu_stream_ring *ring;
	struct xgpu_stream_reply_record *initialize;

	memset(&xgpu_backend_capabilities, 0, sizeof(xgpu_backend_capabilities));
	if (!stream.ring_size)
		stream.ring_size = RING_SIZE_DEFAULT;
	if (!stream.frames_in_flight)
		stream.frames_in_flight = FRAMES_IN_FLIGHT_DEFAULT;
	ring = stream_allocate(64, sizeof(*ring));
	stream.buffer = stream_allocate(XGPU_STREAM_RING_ALIGNMENT, stream.ring_size);
	stream.visibility = stream_allocate(64, XGPU_STREAM_VISIBILITY_SLOTS * sizeof(uint32_t));
	stream.pipelines = stream_allocate(64, XGPU_STREAM_PIPELINES);
	stream.replies = stream_allocate(64, sizeof(*stream.replies));
	if (!ring || !stream.buffer || !stream.visibility || !stream.pipelines || !stream.replies ||
		!numbering_initialize(&stream.pipeline_keys, sizeof(struct xgpu_pipeline_key), XGPU_STREAM_PIPELINES) ||
		!numbering_initialize(&stream.layouts, sizeof(stream.layout), XGPU_STREAM_LAYOUTS))
	{
		fprintf(stderr, "the renderer's command stream: no memory for its ring\n");
		return;
	}
	memset(ring, 0, sizeof(*ring));
	memset((void *)stream.visibility, 0, XGPU_STREAM_VISIBILITY_SLOTS * sizeof(uint32_t));
	memset((void *)stream.pipelines, 0, XGPU_STREAM_PIPELINES);
	ring->magic = XGPU_STREAM_MAGIC;
	ring->version = XGPU_STREAM_VERSION;
	ring->header_size = sizeof(*ring);
	ring->buffer = STREAM_ADDRESS(stream.buffer);
	ring->size = stream.ring_size;
	ring->frames_in_flight = stream.frames_in_flight;
	ring->visibility = STREAM_ADDRESS(stream.visibility);
	ring->pipelines = STREAM_ADDRESS(stream.pipelines);
	stream.ring = ring;
	stream.size = stream.ring_size;
	stream.mask = stream.size - 1;
	stream.inline_limit = stream.size / 8;
	stream.position = 0;
	stream.frame_start = 0;
	while (stream.aside_count)
		stream_free(stream.asides[--stream.aside_count].data);
	memset(&stream.vertices, 0, sizeof(stream.vertices));
	forget_sent();
	if (host_render_stream_open(STREAM_ADDRESS(ring)))
	{
		fprintf(stderr, "the renderer's command stream: the host has no renderer for it\n");
		return;
	}
	stream.open = TRUE;
#if !defined(HALO_METAL) && !defined(XGPU_STREAM_TEST)
	/* (as the game exits, on its thread, the frames submitted played: the
	last one shown, the VR mode's ended, their screenshots written) */
	atexit(xgpu_stream_encode_close);
#endif

	initialize = record(XGPU_STREAM_INITIALIZE, sizeof(*initialize));
	initialize->reply = STREAM_ADDRESS(&stream.replies->capabilities);
	memset(&stream.replies->capabilities, 0, sizeof(stream.replies->capabilities));
	synchronous();
	stream.capabilities = stream.replies->capabilities;
	xgpu_backend_capabilities.largest_target = stream.capabilities.largest_target;
	xgpu_backend_capabilities.most_samples = stream.capabilities.most_samples;
	xgpu_backend_capabilities.most_anisotropy = stream.capabilities.most_anisotropy;
	xgpu_backend_capabilities.sample_counting = stream.capabilities.sample_counting;
	xgpu_backend_capabilities.bgra_vertex_colors = stream.capabilities.bgra_vertex_colors;
	xgpu_backend_capabilities.base_vertex = stream.capabilities.base_vertex;
	xgpu_backend_capabilities.sample_mask = stream.capabilities.sample_mask;
	xgpu_backend_capabilities.mixed_constant_blend = stream.capabilities.mixed_constant_blend;
	xgpu_backend_capabilities.dynamic_lights = stream.capabilities.dynamic_lights;
	xgpu_backend_capabilities.hdr = stream.capabilities.hdr;
}

/* (at exit) the frames submitted played, the render thread ended */
void xgpu_stream_encode_close(void)
{
	if (!stream.open)
		return;
	commit(XGPU_STREAM_SUBMIT_SYNC);
	host_render_stream_close();
	stream.open = FALSE;
}

void xgpu_backend_invalidate(void)
{
	if (!stream.open)
		return;
	record(XGPU_STREAM_INVALIDATE, sizeof(struct xgpu_stream_record));
	forget_sent();
}

void xgpu_backend_flush(void)
{
	if (!stream.open)
		return;
	record(XGPU_STREAM_FLUSH, sizeof(struct xgpu_stream_record));
	/* the host can begin on the frame */
	commit(0);
}

void xgpu_backend_consume(void)
{
	if (!stream.open)
		return;
	record(XGPU_STREAM_FLUSH, sizeof(struct xgpu_stream_record));
	synchronous();
}

void xgpu_backend_anti_aliasing_prepare(BOOL smaa)
{
	if (stream.open)
		record_value(XGPU_STREAM_ANTI_ALIASING_PREPARE, smaa ? 1 : 0);
}

void xgpu_backend_anti_alias(xgpu_target target, BOOL smaa, const long corners[4])
{
	struct xgpu_stream_anti_alias *record_body;
	int index;

	if (!stream.open)
		return;
	record_body = record(XGPU_STREAM_ANTI_ALIAS, sizeof(*record_body));
	record_body->target = target;
	record_body->smaa = smaa ? 1 : 0;
	for (index = 0; index < 4; index++)
		record_body->corners[index] = (int32_t)corners[index];
}

void xgpu_backend_hdr(BOOL active, int mode, int paper_white_nits, int paper_white, int ui_nits)
{
	struct xgpu_stream_hdr *record_body;

	if (!stream.open)
		return;
	record_body = record(XGPU_STREAM_HDR, sizeof(*record_body));
	record_body->active = active ? 1 : 0;
	record_body->mode = (uint32_t)mode;
	record_body->paper_white_nits = (uint32_t)paper_white_nits;
	record_body->paper_white = (uint32_t)paper_white;
	record_body->ui_nits = (uint32_t)ui_nits;
}

void xgpu_backend_gamma(float gamma)
{
	uint32_t bits;

	memcpy(&bits, &gamma, sizeof(bits));
	if (stream.open)
		record_value(XGPU_STREAM_GAMMA, bits);
}

void xgpu_backend_picture_top(int top)
{
	/* (0 centered, n + 1 at n pixels from the top) */
	if (stream.open)
		record_value(XGPU_STREAM_PICTURE_POSITION, top < 0 ? 0 : (uint32_t)top + 1);
}

void xgpu_backend_tone_map(xgpu_target target, const long corners[4])
{
	struct xgpu_stream_tone_map *record_body;
	int index;

	if (!stream.open)
		return;
	record_body = record(XGPU_STREAM_TONE_MAP, sizeof(*record_body));
	record_body->target = target;
	for (index = 0; index < 4; index++)
		record_body->corners[index] = (int32_t)corners[index];
}

void xgpu_backend_ambient_occlusion(xgpu_target target, const long corners[4], int quality, const float camera[6])
{
	struct xgpu_stream_ambient_occlusion *record_body;
	int index;

	if (!stream.open)
		return;
	record_body = record(XGPU_STREAM_AMBIENT_OCCLUSION, sizeof(*record_body));
	record_body->target = target;
	record_body->quality = (uint32_t)quality;
	for (index = 0; index < 4; index++)
		record_body->corners[index] = (int32_t)corners[index];
	for (index = 0; index < 6; index++)
		record_body->camera[index] = camera ? camera[index] : 0.0f;
}

/* (the host writes the ring's display_hdr and display_headroom as it looks
at its display) */
BOOL xgpu_backend_display_hdr(float *headroom)
{
	uint32_t bits;
	float value;

	*headroom = 1.0f;
	if (!stream.open)
		return FALSE;
	bits = __atomic_load_n(&stream.ring->display_headroom, __ATOMIC_ACQUIRE);
	memcpy(&value, &bits, sizeof(value));
	if (value >= 1.0f && value < 1000.0f)
		*headroom = value;
	return __atomic_load_n(&stream.ring->display_hdr, __ATOMIC_ACQUIRE) != 0;
}

/* (and display_white, display_white_reported) */
BOOL xgpu_backend_display_white(float *nits)
{
	uint32_t bits;
	float value;

	*nits = 0.0f;
	if (!stream.open)
		return FALSE;
	bits = __atomic_load_n(&stream.ring->display_white, __ATOMIC_ACQUIRE);
	memcpy(&value, &bits, sizeof(value));
	if (value > 0.0f && value < 100000.0f)
		*nits = value;
	return __atomic_load_n(&stream.ring->display_white_reported, __ATOMIC_ACQUIRE) != 0;
}

/* ---------- render targets */

void xgpu_backend_target_create(xgpu_target target, const struct xgpu_target_description *description)
{
	struct xgpu_stream_target_create *record_body;

	if (!stream.open)
		return;
	record_body = record(XGPU_STREAM_TARGET_CREATE, sizeof(*record_body));
	record_body->target = target;
	record_body->width = (uint32_t)description->width;
	record_body->height = (uint32_t)description->height;
	record_body->depth = description->depth ? 1 : 0;
	record_body->layers = (uint8_t)description->layers;
	record_body->format = (uint8_t)(description->depth ? XGPU_FORMAT_D24S8 :
		description->format ? description->format : XGPU_FORMAT_BGRA8);
	if (target < TARGET_MAXIMUM)
	{
		stream.targets[target].width = record_body->width;
		stream.targets[target].height = record_body->height;
	}
}

void xgpu_backend_target_delete(xgpu_target target)
{
	if (!stream.open)
		return;
	record_value(XGPU_STREAM_TARGET_DELETE, target);
	if (target < TARGET_MAXIMUM)
		stream.targets[target].width = stream.targets[target].height = 0;
}

void xgpu_backend_target_multisample(xgpu_target target, int samples)
{
	struct xgpu_stream_target_multisample *record_body;

	if (!stream.open)
		return;
	record_body = record(XGPU_STREAM_TARGET_MULTISAMPLE, sizeof(*record_body));
	record_body->target = target;
	record_body->samples = samples;
}

void xgpu_backend_target_resolve(xgpu_target target)
{
	if (stream.open)
		record_value(XGPU_STREAM_TARGET_RESOLVE, target);
}

void xgpu_backend_set_targets(xgpu_target color, int hud_group, xgpu_target depth, int views, int base, int samples)
{
	struct xgpu_stream_set_targets *record_body;

	if (!stream.open)
		return;
	preparation_commit();
	record_body = record(XGPU_STREAM_SET_TARGETS, sizeof(*record_body));
	record_body->color = color;
	record_body->depth = depth;
	record_body->hud_group = (int8_t)hud_group;
	record_body->views = (int8_t)views;
	record_body->base = (uint8_t)base;
	record_body->samples = (uint8_t)samples;
}

void xgpu_backend_clear(unsigned long flags, const float color[4], float depth, unsigned long stencil,
	const long (*rectangles)[4], unsigned long count)
{
	struct xgpu_stream_clear *record_body;
	unsigned long index;
	int corner;

	if (!stream.open)
		return;
	/* (rectangles are few: the viewport's, a split screen's) */
	if (count > 256)
		count = 256;
	record_body = record(XGPU_STREAM_CLEAR, (uint32_t)(sizeof(*record_body) + count * sizeof(record_body->rectangles[0])));
	record_body->flags = (uint32_t)flags;
	memcpy(record_body->color, color, sizeof(record_body->color));
	record_body->depth = depth;
	record_body->stencil = (uint32_t)stencil;
	record_body->count = (uint32_t)count;
	for (index = 0; index < count; index++)
	{
		for (corner = 0; corner < 4; corner++)
			record_body->rectangles[index][corner] = (int32_t)rectangles[index][corner];
	}
}

void xgpu_backend_clear_target(xgpu_target target, const float color[4])
{
	struct xgpu_stream_clear_target *record_body;

	if (!stream.open)
		return;
	record_body = record(XGPU_STREAM_CLEAR_TARGET, sizeof(*record_body));
	record_body->target = target;
	memcpy(record_body->color, color, sizeof(record_body->color));
}

unsigned char *xgpu_backend_read_target(xgpu_target target, int layer, BOOL tone_map)
{
	struct xgpu_stream_read_target *record_body;
	unsigned char *pixels;
	uint32_t size;

	if (!stream.open || target >= TARGET_MAXIMUM || !stream.targets[target].width)
		return NULL;
	size = stream.targets[target].width * stream.targets[target].height * 4;
	pixels = stream_allocate(16, size);
	if (!pixels)
		return NULL;
	record_body = record(XGPU_STREAM_READ_TARGET, sizeof(*record_body));
	record_body->target = target;
	record_body->layer = layer;
	record_body->tone_map = tone_map ? 1 : 0;
	record_body->pixels = STREAM_ADDRESS(pixels);
	record_body->size = size;
	record_body->reply = STREAM_ADDRESS(&stream.replies->value);
	stream.replies->value = 0;
	synchronous();
	if (!stream.replies->value)
	{
		stream_free(pixels);
		return NULL;
	}
	return pixels;
}

BOOL xgpu_backend_screenshot(xgpu_target target, const char *directory, char *name, unsigned long name_size)
{
	struct xgpu_stream_screenshot *record_body;
	unsigned char *pixels;
	char *written_name;
	uint32_t size, length;
	BOOL written;

	if (!name || !name_size)
		return FALSE;
	name[0] = 0;
	if (!stream.open || target >= TARGET_MAXIMUM || !stream.targets[target].width || !directory || !directory[0])
		return FALSE;
	length = (uint32_t)strlen(directory);
	if (length > 4096)
		return FALSE;
	size = stream.targets[target].width * stream.targets[target].height * 4;
	/* (the pixels, then the name the host writes) */
	pixels = stream_allocate(16, size + XGPU_STREAM_SCREENSHOT_NAME);
	if (!pixels)
		return FALSE;
	written_name = (char *)pixels + size;
	record_body = record(XGPU_STREAM_SCREENSHOT, (uint32_t)sizeof(*record_body) + length + 1);
	record_body->target = target;
	record_body->width = stream.targets[target].width;
	record_body->height = stream.targets[target].height;
	record_body->pixels = STREAM_ADDRESS(pixels);
	record_body->size = size;
	record_body->name = STREAM_ADDRESS(written_name);
	record_body->reply = STREAM_ADDRESS(&stream.replies->value);
	memcpy(record_body->directory, directory, length + 1);
	written_name[0] = 0;
	stream.replies->value = 0;
	synchronous();
	written = stream.replies->value != 0;
	written_name[XGPU_STREAM_SCREENSHOT_NAME - 1] = 0;
	snprintf(name, name_size, "%s", written_name);
	stream_free(pixels);
	return written;
}

/* ---------- textures */

void xgpu_backend_texture_describe(DWORD resource, DWORD palette, struct xgpu_texture_description *description,
	int *kind)
{
	const D3DBaseTexture *header = INTERFACE_POINTER(resource);

	(void)palette;
	xgpu_texture_describe(header->Format, header->Size, description);
	*kind = description->cube_map ? XGPU_TEXTURE_KIND_CUBE : description->depth > 1 ? XGPU_TEXTURE_KIND_3D :
		XGPU_TEXTURE_KIND_2D;
	/* (a high-res replacement's levels, and what it is) */
#ifndef XGPU_STREAM_TEST
	xgpu_texture_replacement(resource, description);
#endif
}

static void texture_stage_describe(const struct xgpu_texture_source *source, int stage_index,
	struct xgpu_stream_texture_stage *stage, struct xgpu_stream_texture_level *levels)
{
	uint32_t index;

#ifdef XGPU_STREAM_TEST
	(void)stage_index;
#endif
	memset(stage, 0, sizeof(*stage));
	stage->source = source->source;
	stage->sampler_type = source->sampler_type;
	stage->sampled = source->sampled;
	stage->resource = (uint32_t)source->resource;
	stage->palette = (uint32_t)source->palette;
	stage->target = source->target;
	stage->drawn = source->drawn;
	stage->data = (uint32_t)source->data;
	stage->width = (uint32_t)source->width;
	stage->height = (uint32_t)source->height;
	stage->levels = (uint32_t)source->levels;
	stage->rendered_levels = (uint32_t)source->rendered_levels;
	stage->level_count = (uint8_t)(source->rendered_levels < XGPU_MIP_LEVELS ? source->rendered_levels : XGPU_MIP_LEVELS);
	for (index = 0; index < stage->level_count; index++)
	{
		levels[index].target = source->level_targets[index];
		levels[index].written = (uint32_t)source->level_written[index];
	}
	stage->sampler.minify = source->sampler.minify;
	stage->sampler.mip = source->sampler.mip;
	stage->sampler.magnify = source->sampler.magnify;
	memcpy(stage->sampler.address, source->sampler.address, sizeof(stage->sampler.address));
	stage->sampler.lod_bias = source->sampler.lod_bias;
	stage->sampler.first_level = (uint32_t)source->sampler.first_level;
	stage->sampler.anisotropy = (uint32_t)source->sampler.anisotropy;
	stage->sampler.border_color = (uint32_t)source->sampler.border_color;
	if (source->source == XGPU_SOURCE_TEXTURE && source->resource)
	{
		const D3DBaseTexture *header = INTERFACE_POINTER(source->resource);
#ifndef XGPU_STREAM_TEST
		struct xgpu_texture_description description;
		const struct xgpu_image *image;
		unsigned int number;

		/* (a high-res replacement drawn in its place: its image) */
		xgpu_texture_describe(header->Format, header->Size, &description);
		number = xgpu_texture_replacement(source->resource, &description);
		if ((image = xgpu_image_get(number)) != NULL)
		{
			stage->source = XGPU_STREAM_SOURCE_IMAGE;
			stage->resource = number;
			stage->resource_format = (uint32_t)image->format;
			stage->resource_size = (uint32_t)image->serial;
			stage->data = STREAM_ADDRESS(image->pixels);
			stage->width = (uint32_t)image->width;
			stage->height = (uint32_t)image->height;
			stage->levels = (uint32_t)image->levels;
			stage->palette = 0;
		}
		else
#endif
		{
			stage->resource_format = (uint32_t)header->Format;
			stage->resource_size = (uint32_t)header->Size;
#ifndef XGPU_STREAM_TEST
			stage->target = (uint32_t)xgpu_texture_channels((unsigned long)PLATFORM_PHYSICAL_TO_VIRTUAL(header->Data),
				stage_index);
#endif
		}
	}
}

void xgpu_backend_set_textures(const struct xgpu_texture_source stages[4])
{
	struct xgpu_stream_texture_stage described[4];
	struct xgpu_stream_texture_level levels[4][XGPU_STREAM_MIP_LEVELS];
	struct xgpu_stream_set_textures *record_body;
	uint32_t changed = 0;
	uint32_t size = sizeof(*record_body);
	unsigned char *place;
	int stage;

	if (!stream.open)
		return;
	for (stage = 0; stage < 4; stage++)
	{
		uint32_t level_bytes;

		texture_stage_describe(&stages[stage], stage, &described[stage], levels[stage]);
		level_bytes = described[stage].level_count * sizeof(levels[0][0]);
		if (stream.stages_sent[stage] && !memcmp(&described[stage], &stream.stages[stage], sizeof(described[stage])) &&
			!memcmp(levels[stage], stream.levels[stage], level_bytes))
			continue;
		changed |= 1u << stage;
		size += sizeof(described[stage]) + level_bytes;
	}
	if (!changed)
		return;
	record_body = record(XGPU_STREAM_SET_TEXTURES, size);
	record_body->changed = changed;
	place = (unsigned char *)(record_body + 1);
	for (stage = 0; stage < 4; stage++)
	{
		uint32_t level_bytes = described[stage].level_count * sizeof(levels[0][0]);

		if (!(changed & (1u << stage)))
			continue;
		memcpy(place, &described[stage], sizeof(described[stage]));
		place += sizeof(described[stage]);
		memcpy(place, levels[stage], level_bytes);
		place += level_bytes;
		stream.stages[stage] = described[stage];
		memcpy(stream.levels[stage], levels[stage], level_bytes);
		stream.stages_sent[stage] = TRUE;
	}
}

/* ---------- pipelines */

/* ---------- numbers (above) */

/* the floats made finite: each not a number or infinite made 0, and said
(what, and the first's register or place) once in a thousand */
static void stream_finite(float *values, unsigned long count, const char *what, unsigned long first)
{
	static unsigned long made;
	unsigned long index;

	for (index = 0; index < count; index++)
	{
		uint32_t bits;

		memcpy(&bits, &values[index], sizeof(bits));
		if ((bits & 0x7f800000u) != 0x7f800000u)
			continue;
		if (made++ % 1000 == 0)
			platform_log("GPU: %s %lu.%c's %s (%08x) drawn as 0 (%lu so far)", what, first + index / 4,
				"xyzw"[index % 4], bits & 0x007fffffu ? "number not a number" : "infinity", (unsigned)bits, made);
		values[index] = 0.0f;
	}
}

void xgpu_backend_set_constants(unsigned long first, const float (*values)[4], unsigned long count)
{
	struct xgpu_stream_constants *record_body;

	if (!stream.open || !count)
		return;
	record_body = record(XGPU_STREAM_CONSTANTS, (uint32_t)(sizeof(*record_body) + count * sizeof(record_body->values[0])));
	record_body->first = (uint32_t)first;
	record_body->count = (uint32_t)count;
	memcpy(record_body->values, values, count * sizeof(record_body->values[0]));
	stream_finite(&record_body->values[0][0], count * 4, "vertex constant", first);
}

void xgpu_backend_vertex_program(unsigned long id, const struct xgpu_vertex_program *program)
{
	struct xgpu_stream_vertex_program *record_body;
	uint32_t count = (uint32_t)program->instruction_count;

	if (!stream.open)
		return;
	if (count > XGPU_STREAM_VERTEX_PROGRAM_MAXIMUM)
		count = XGPU_STREAM_VERTEX_PROGRAM_MAXIMUM;
	record_body = record(XGPU_STREAM_VERTEX_PROGRAM, sizeof(*record_body) + count * XGPU_STREAM_VERTEX_INSTRUCTION_SIZE);
	record_body->id = (uint32_t)id;
	record_body->hash[0] = (uint32_t)program->hash;
	record_body->hash[1] = (uint32_t)(program->hash >> 32);
	record_body->instruction_count = count;
	record_body->projects = program->projects;
	memcpy(record_body->instructions, program->instructions, count * XGPU_STREAM_VERTEX_INSTRUCTION_SIZE);
}

void xgpu_backend_vertex_lighting(unsigned long long program, const struct nv2a_vertex_lighting *lighting)
{
	struct xgpu_stream_vertex_lighting *record_body;

	if (!stream.open)
		return;
	record_body = record(XGPU_STREAM_VERTEX_LIGHTING, sizeof(*record_body));
	record_body->hash[0] = (uint32_t)program;
	record_body->hash[1] = (uint32_t)(program >> 32);
	record_body->lights = lighting->lights;
	record_body->normal_instruction = (uint32_t)lighting->normal_instruction;
	record_body->normal_register = (uint32_t)lighting->normal_register;
	record_body->position_instruction = (uint32_t)lighting->position_instruction;
	record_body->position_register = (uint32_t)lighting->position_register;
	record_body->world_found = lighting->world_found ? 1 : 0;
	record_body->world_instruction = (uint32_t)lighting->world_instruction;
	record_body->world_register = (uint32_t)lighting->world_register;
}

BOOL xgpu_backend_pipeline(const struct xgpu_pipeline_key *key)
{
	uint32_t id;
	BOOL made;

	if (!stream.open)
		return FALSE;
	/* (numbered by the key as the front end has it, zeroed then filled:
	made the stream's only when it is new) */
	if (stream.pipeline && !memcmp(key, &stream.last_key, sizeof(*key)))
		return __atomic_load_n(&stream.pipelines[stream.pipeline], __ATOMIC_RELAXED) != XGPU_STREAM_PIPELINE_FAILED;
	/* (the last key found is last_key, unless the numbers began again) */
	id = numbering_find(&stream.pipeline_keys, key, &made,
		stream.pipeline && stream.pipeline_keys.last == stream.pipeline);
	memcpy(&stream.last_key, key, sizeof(*key));
	if (made)
	{
		struct xgpu_stream_pipeline_define *define = record(XGPU_STREAM_PIPELINE_DEFINE, sizeof(*define));
		struct xgpu_pixel_program program;

		xgpu_stream_pipeline_key_describe(key, &stream.key);
		define->id = id;
		define->key = stream.key;
		nv2a_pixel_program_decode(&key->pixel, &program);
		xgpu_stream_pixel_program_describe(&program, &define->pixel_program);
		/* (the host's answer comes later) */
		__atomic_store_n(&stream.pipelines[id], XGPU_STREAM_PIPELINE_UNKNOWN, __ATOMIC_RELAXED);
		stream.pipeline = 0;
	}
	if (id != stream.pipeline)
	{
		record_value(XGPU_STREAM_PIPELINE, id);
		stream.pipeline = id;
	}
	return __atomic_load_n(&stream.pipelines[id], __ATOMIC_RELAXED) != XGPU_STREAM_PIPELINE_FAILED;
}

unsigned long xgpu_backend_pipeline_number(void)
{
	/* (XGPU_STREAM_PIPELINES: 14 bits) */
	return (unsigned long)stream.pipeline | (unsigned long)stream.pipeline_keys.rounds << 16;
}

static void dynamic_state_describe(const struct xgpu_dynamic_state *dynamic, struct xgpu_stream_dynamic_state *wire)
{
	int index;

	for (index = 0; index < 4; index++)
	{
		wire->viewport[index] = (int32_t)dynamic->viewport[index];
		wire->scissor[index] = (int32_t)dynamic->scissor[index];
		wire->blend_color[index] = dynamic->blend_color[index];
	}
	wire->depth_range[0] = dynamic->depth_range[0];
	wire->depth_range[1] = dynamic->depth_range[1];
	wire->stencil_reference = (uint32_t)dynamic->stencil_reference;
	wire->depth_bias[0] = dynamic->depth_bias[0];
	wire->depth_bias[1] = dynamic->depth_bias[1];
}

void xgpu_backend_draw_state(const struct xgpu_dynamic_state *dynamic, const struct xgpu_draw_uniforms *uniforms,
	unsigned long uniforms_serial)
{
	const unsigned char *now = (const unsigned char *)uniforms;
	unsigned char *had = (unsigned char *)&stream.uniforms;
	struct xgpu_stream_dynamic_state wire;
	struct xgpu_stream_draw_state *record_body;
	uint32_t parts = 0, pieces[2] = {0, 0};
	uint32_t size = sizeof(*record_body);
	unsigned char *place;
	uint32_t piece;

	if (!stream.open)
		return;
	memset(&wire, 0, sizeof(wire));
	dynamic_state_describe(dynamic, &wire);
	if (!stream.dynamic_sent || memcmp(&wire, &stream.dynamic, sizeof(wire)))
	{
		parts |= XGPU_STREAM_DRAW_DYNAMIC;
		size += sizeof(wire);
	}
	if (!stream.uniforms_sent || (uint32_t)uniforms_serial != stream.uniforms_serial)
	{
		/* (the pieces that changed: a few constants of the combiners, the
		viewport's, a texture's scale) */
		for (piece = 0; piece < XGPU_STREAM_UNIFORM_PIECES; piece++)
		{
			uint32_t offset = piece * 16;
			uint32_t bytes = sizeof(stream.uniforms) - offset < 16 ? sizeof(stream.uniforms) - offset : 16;

			if (!stream.uniforms_sent || memcmp(now + offset, had + offset, bytes))
			{
				pieces[piece / 32] |= 1u << (piece % 32);
				size += 16;
			}
		}
		parts |= XGPU_STREAM_DRAW_UNIFORMS;
	}
	if (!parts)
		return;
	record_body = record(XGPU_STREAM_DRAW_STATE, size);
	record_body->parts = parts;
	record_body->uniforms_serial = (uint32_t)uniforms_serial;
	record_body->pieces[0] = pieces[0];
	record_body->pieces[1] = pieces[1];
	place = (unsigned char *)(record_body + 1);
	if (parts & XGPU_STREAM_DRAW_DYNAMIC)
	{
		memcpy(place, &wire, sizeof(wire));
		place += sizeof(wire);
		stream.dynamic = wire;
		stream.dynamic_sent = TRUE;
	}
	for (piece = 0; piece < XGPU_STREAM_UNIFORM_PIECES; piece++)
	{
		uint32_t offset = piece * 16;
		uint32_t bytes = sizeof(stream.uniforms) - offset < 16 ? sizeof(stream.uniforms) - offset : 16;

		if (!(pieces[piece / 32] & (1u << (piece % 32))))
			continue;
		memcpy(place, now + offset, bytes);
		memcpy(had + offset, now + offset, bytes);
		stream_finite((float *)place, bytes / 4, "draw uniform", piece * 4);
		place += 16;
	}
	if (parts & XGPU_STREAM_DRAW_UNIFORMS)
	{
		stream.uniforms_serial = (uint32_t)uniforms_serial;
		stream.uniforms_sent = TRUE;
	}
}

/* ---------- vertices and draws */

void xgpu_backend_mirror_write(unsigned long segment, unsigned long offset, unsigned long size, const void *data,
	BOOL unused)
{
	struct xgpu_stream_mirror_write *record_body;
	unsigned char *place;

	if (!stream.open || !size)
		return;
	record_body = record_data(XGPU_STREAM_MIRROR_WRITE, sizeof(*record_body), carried_one((uint32_t)size), &place);
	record_body->segment = (uint32_t)segment;
	record_body->offset = (uint32_t)offset;
	record_body->size = (uint32_t)size;
	record_body->unused = unused ? 1 : 0;
	record_body->data = carry_one(&place, data, (uint32_t)size);
}

/* the registers of values that differ from what the host has, those
values now its */
static uint32_t attributes_changed(struct attributes *host, const float (*values)[4], uint32_t registers)
{
	uint32_t changed = 0;
	int reg;

	for (reg = 0; reg < XGPU_VERTEX_ATTRIBUTE_COUNT; reg++)
	{
		if (!(registers & (1u << reg)))
			continue;
		if (!(host->sent & (1u << reg)) || memcmp(host->values[reg], values[reg], sizeof(values[reg])))
		{
			memcpy(host->values[reg], values[reg], sizeof(values[reg]));
			changed |= 1u << reg;
		}
	}
	host->sent |= changed;
	return changed;
}

static void attributes_write(unsigned char *place, const struct attributes *host, uint32_t changed)
{
	int reg;

	for (reg = 0; reg < XGPU_VERTEX_ATTRIBUTE_COUNT; reg++)
	{
		if (changed & (1u << reg))
		{
			memcpy(place, host->values[reg], sizeof(host->values[reg]));
			place += sizeof(host->values[reg]);
		}
	}
}

static int bits(uint32_t value)
{
	return __builtin_popcount(value);
}

static void vertices_write(uint32_t layout, uint32_t present, const struct xgpu_stream_vertex_stream wire[16],
	const void *const sources[16], uint32_t changed);

void xgpu_backend_set_vertices(const struct xgpu_vertex_input *input)
{
	struct xgpu_stream_layout_define *layout = (struct xgpu_stream_layout_define *)stream.layout;
	struct xgpu_stream_vertex_stream wire[16];
	const void *sources[16];
	uint32_t element_count = (uint32_t)input->element_count;
	uint32_t present = 0, fed = 0, changed;
	uint32_t id, index;
	BOOL made;
	int s;

	if (!stream.open)
		return;
	preparation_commit();
	if (element_count > XGPU_VERTEX_ATTRIBUTE_COUNT)
		element_count = XGPU_VERTEX_ATTRIBUTE_COUNT;
	memset(stream.layout, 0, sizeof(stream.layout));
	layout->declaration = (uint32_t)input->declaration;
	layout->element_count = element_count;
	layout->packed_mask = (uint32_t)input->packed_mask;
	memcpy(layout->elements, input->elements, element_count * sizeof(layout->elements[0]));
	for (index = 0; index < element_count; index++)
		fed |= 1u << (input->elements[index].reg & 15);
	id = numbering_find(&stream.layouts, stream.layout, &made, FALSE);
	if (made)
	{
		struct xgpu_stream_layout_define *define = record(XGPU_STREAM_LAYOUT_DEFINE,
			align_up(sizeof(*define) + element_count * sizeof(define->elements[0]), XGPU_STREAM_RECORD_ALIGNMENT));

		define->id = id;
		define->declaration = layout->declaration;
		define->element_count = element_count;
		define->packed_mask = layout->packed_mask;
		memcpy(define->elements, layout->elements, element_count * sizeof(define->elements[0]));
	}
	memset(wire, 0, sizeof(wire));
	memset(sources, 0, sizeof(sources));

	for (s = 0; s < 16; s++)
	{
		if (!input->streams[s].present)
			continue;
		present |= 1u << s;
		wire[s].segment = (int32_t)input->streams[s].segment;
		wire[s].offset = (uint32_t)input->streams[s].offset;
		wire[s].bytes = (uint32_t)input->streams[s].bytes;
		wire[s].stride = (uint32_t)input->streams[s].stride;
		sources[s] = wire[s].segment < 0 ? INTERFACE_POINTER(input->streams[s].data) : NULL;
	}
	changed = attributes_changed(&stream.vertex_attributes, input->attributes, ~fed & 0xffff);
	vertices_write(id, present, wire, sources, changed);
}

/* the VERTICES record: the layout's id, the streams present (their
streamed data at sources, copied into the record, or aside if too large;
sent again, sources NULL: as the last record had them), the inputs' values
that changed */
static void vertices_write(uint32_t layout, uint32_t present, const struct xgpu_stream_vertex_stream wire[16],
	const void *const sources[16], uint32_t changed)
{
	BOOL again = sources == NULL;
	struct xgpu_stream_vertices *record_body;
	struct xgpu_stream_vertex_stream *streams;
	uint32_t streamed = 0, data_size = 0, fixed;
	BOOL inline_data = TRUE;
	unsigned char *place;
	int s;

	for (s = 0; s < 16; s++)
	{
		if ((present & (1u << s)) && wire[s].segment < 0)
			streamed += align_up(wire[s].bytes, XGPU_STREAM_DATA_ALIGNMENT);
	}
	if (again)
	{
		/* (as before: inline again if it was, else the same buffers aside) */
		inline_data = stream.vertices.inline_data;
		data_size = inline_data ? streamed : 0;
	}
	else if (streamed)
	{
		data_size = carried(streamed);
		inline_data = data_size != 0;
		if (!inline_data)
			asides_reserve((uint32_t)bits(present));
	}
	fixed = sizeof(*record_body) + bits(present) * sizeof(*streams) +
		bits(changed) * sizeof(stream.vertex_attributes.values[0]);
	record_body = record_data(XGPU_STREAM_VERTICES, fixed, data_size, &place);
	if (!again)
	{
		/* (the buffers aside the last one named are the host's until it
		has this one) */
		asides_unpin();
	}
	record_body->layout = layout;
	record_body->streams = (uint16_t)present;
	record_body->attributes = (uint16_t)changed;
	streams = (struct xgpu_stream_vertex_stream *)(record_body + 1);
	for (s = 0; s < 16; s++)
	{
		if (!(present & (1u << s)))
			continue;
		*streams = wire[s];
		if (wire[s].segment < 0)
		{
			const void *source = again ? STREAM_POINTER(wire[s].data) : sources[s];

			if (inline_data)
				streams->data = carry(&place, source, wire[s].bytes);
			else if (!again)
				streams->data = carry_pinned(NULL, source, wire[s].bytes, TRUE);
		}
		stream.vertices.streams[s] = *streams;
		streams++;
	}
	attributes_write((unsigned char *)streams, &stream.vertex_attributes, changed);
	stream.vertices.streamed = streamed != 0;
	stream.vertices.inline_data = inline_data;
	stream.vertices.batch = stream.batch;
	stream.vertices.position = stream.position - record_size(record_body);
	stream.vertices.fixed = fixed;
	stream.vertices.size = fixed + (inline_data ? streamed + XGPU_STREAM_DATA_ALIGNMENT : 0);
	stream.vertices.layout = layout;
	stream.vertices.present = present;
}

/* before size bytes are written from position: if they would reach the
last VERTICES record's (a ring later), its streamed data is set aside
(the draws after it may need it sent again: vertices_ensure), and the
host's copy is no longer the one to read */
static void vertices_keep(uint32_t size)
{
	int s;

	if (!stream.vertices.streamed || !stream.vertices.inline_data ||
		stream.position + size - stream.vertices.position <= stream.size)
		return;
	for (s = 0; s < 16; s++)
	{
		struct xgpu_stream_vertex_stream *kept = &stream.vertices.streams[s];
		void *copy;

		if (!(stream.vertices.present & (1u << s)) || kept->segment >= 0)
			continue;
		copy = stream_allocate(XGPU_STREAM_DATA_ALIGNMENT, align_up(kept->bytes, XGPU_STREAM_DATA_ALIGNMENT));
		if (!copy)
		{
			/* (no memory: not sent again) */
			stream.vertices.streamed = FALSE;
			return;
		}
		memcpy(copy, STREAM_POINTER(kept->data), kept->bytes);
		kept->data = aside_pinned(copy, TRUE);
	}
	stream.vertices.inline_data = FALSE;
	stream.vertices.size = stream.vertices.fixed;
	/* (the next draw sends them again) */
	stream.vertices.batch = stream.batch - 1;
}

/* before a draw of size bytes (its record, with its data): the VERTICES
record it reads is in the same submit as it, sent again if a submit came
between (the host may release a submit's bytes before the next is
played), and no submit comes before the draw is written */
static void vertices_ensure(uint32_t size)
{
	if (!stream.vertices.streamed)
		return;
	/* (twice: either may be preceded by a skip to the ring's end, no
	larger than itself) */
	room(2 * size + 2 * XGPU_STREAM_DATA_ALIGNMENT);
	if (stream.vertices.batch == stream.batch)
		return;
	room(2 * (stream.vertices.size + size) + 2 * XGPU_STREAM_DATA_ALIGNMENT);
	{
		/* (from the copy: the next write would overwrite it) */
		struct xgpu_stream_vertex_stream last[16];

		memcpy(last, stream.vertices.streams, sizeof(last));
		stream.ring->vertices_again++;
		vertices_write(stream.vertices.layout, stream.vertices.present, last, NULL, 0);
	}
}

void xgpu_backend_draw(int primitive, unsigned long count)
{
	struct xgpu_stream_draw *record_body;

	if (!stream.open)
		return;
	vertices_ensure(sizeof(*record_body));
	record_body = record(XGPU_STREAM_DRAW, sizeof(*record_body));
	record_body->primitive = (uint32_t)primitive;
	record_body->count = (uint32_t)count;
}

void xgpu_backend_draw_indices(int primitive, const WORD *indices, unsigned long count)
{
	struct xgpu_stream_draw_indices *record_body;
	uint32_t bytes = (uint32_t)count * sizeof(WORD), data_size;
	unsigned char *place;

	if (!stream.open)
		return;
	data_size = carried_one(bytes);
	vertices_ensure(sizeof(*record_body) + data_size + XGPU_STREAM_DATA_ALIGNMENT);
	record_body = record_data(XGPU_STREAM_DRAW_INDICES, sizeof(*record_body), data_size, &place);
	record_body->primitive = (uint32_t)primitive;
	record_body->count = (uint32_t)count;
	record_body->indices = carry_one(&place, indices, bytes);
}

void xgpu_backend_draw_indexed(int primitive, const WORD *indices, unsigned long count, unsigned long minimum,
	long segment, unsigned long offset, BOOL temporary)
{
	struct xgpu_stream_draw_indexed *record_body;
	uint32_t bytes = segment < 0 ? (uint32_t)count * sizeof(WORD) : 0, data_size;
	unsigned char *place;

	if (!stream.open)
		return;
	data_size = carried_one(bytes);
	vertices_ensure(sizeof(*record_body) + data_size + XGPU_STREAM_DATA_ALIGNMENT);
	record_body = record_data(XGPU_STREAM_DRAW_INDEXED, sizeof(*record_body), data_size, &place);
	record_body->primitive = (uint32_t)primitive;
	record_body->count = (uint32_t)count;
	record_body->minimum = (uint32_t)minimum;
	record_body->segment = (int32_t)segment;
	record_body->offset = (uint32_t)offset;
	record_body->temporary = temporary ? 1 : 0;
	record_body->indices = carry_one(&place, indices, bytes);
}

void xgpu_backend_draw_immediate(int primitive, const float *vertices, unsigned long count, unsigned long written,
	const float (*attributes)[4], const WORD *indices, unsigned long index_count)
{
	struct xgpu_stream_draw_immediate *record_body;
	uint32_t registers = (uint32_t)written & 0xffff;
	uint32_t vertex_bytes = (uint32_t)count * bits(registers) * 16;
	uint32_t index_bytes = (uint32_t)index_count * sizeof(WORD);
	uint32_t changed, data_size, packed_bytes;
	float *packed = NULL;
	unsigned char *place;
	unsigned long vertex;

	if (!stream.open)
		return;
	/* the written registers' values alone, vertex by vertex (immediate
	mode's vertices are few: lens flares, text, the HUD) */
	changed = attributes_changed(&stream.immediate_attributes, attributes, ~registers & 0xffff);
	packed_bytes = align_up(vertex_bytes, XGPU_STREAM_DATA_ALIGNMENT);
	data_size = carried(packed_bytes) + carried(index_bytes);
	if (packed_bytes > stream.inline_limit || index_bytes > stream.inline_limit)
		asides_reserve(2);
	if (packed_bytes > stream.inline_limit)
	{
		packed = stream_allocate(XGPU_STREAM_DATA_ALIGNMENT, packed_bytes);
		if (!packed)
			return;
	}
	record_body = record_data(XGPU_STREAM_DRAW_IMMEDIATE, sizeof(*record_body) +
		bits(changed) * sizeof(stream.immediate_attributes.values[0]), data_size, &place);
	record_body->primitive = (uint32_t)primitive;
	record_body->count = (uint32_t)count;
	record_body->written = (uint16_t)registers;
	record_body->attributes = (uint16_t)changed;
	record_body->index_count = (uint32_t)index_count;
	attributes_write((unsigned char *)(record_body + 1), &stream.immediate_attributes, changed);
	if (vertex_bytes)
	{
		float *values = packed ? packed : (float *)place;

		for (vertex = 0; vertex < count; vertex++)
		{
			int reg;

			for (reg = 0; reg < XGPU_VERTEX_ATTRIBUTE_COUNT; reg++)
			{
				if (registers & (1u << reg))
				{
					memcpy(values, vertices + (vertex * XGPU_VERTEX_ATTRIBUTE_COUNT + reg) * 4, 16);
					values += 4;
				}
			}
		}
		stream_finite(packed ? packed : (float *)place, (unsigned long)count * bits(registers) * 4, "immediate vertex value",
			0);
		if (packed)
		{
			record_body->vertices = aside(packed);
		}
		else
		{
			record_body->vertices = STREAM_ADDRESS(place);
			place += packed_bytes;
		}
	}
	record_body->indices = carry_one(&place, indices, index_bytes);
}

/* ---------- visibility tests */

void xgpu_backend_visibility_begin(void)
{
	if (stream.open)
		record(XGPU_STREAM_VISIBILITY_BEGIN, sizeof(struct xgpu_stream_record));
}

void xgpu_backend_visibility_end(unsigned long slot, float area, int views, int samples)
{
	struct xgpu_stream_visibility_end *record_body;

	if (!stream.open)
		return;
	record_body = record(XGPU_STREAM_VISIBILITY_END, sizeof(*record_body));
	record_body->slot = (uint32_t)slot;
	record_body->area = area;
	record_body->views = views;
	record_body->samples = samples;
}

UINT xgpu_backend_visibility_result(unsigned long slot)
{
	if (!stream.open || slot >= XGPU_STREAM_VISIBILITY_SLOTS)
		return 0;
	return __atomic_load_n(&stream.visibility[slot], __ATOMIC_RELAXED);
}

void xgpu_backend_visibility_frame(void)
{
	if (stream.open)
		record(XGPU_STREAM_VISIBILITY_FRAME, sizeof(struct xgpu_stream_record));
}

/* ---------- frames */

void xgpu_backend_present(xgpu_target back_buffer)
{
	if (stream.open)
		record_value(XGPU_STREAM_PRESENT, back_buffer);
}

void xgpu_backend_frame_end(void)
{
	struct xgpu_stream_frame_end *record_body;
	uint64_t input = 0;

	if (!stream.open)
		return;
#ifndef XGPU_STREAM_TEST
	input = d3d_frame_input_time();
#endif
	record_body = record(XGPU_STREAM_FRAME_END, sizeof(*record_body));
	record_body->input_low = (uint32_t)input;
	record_body->input_high = (uint32_t)(input >> 32);
	commit(XGPU_STREAM_SUBMIT_FRAME);
#ifndef XGPU_STREAM_TEST
	/* (the deleted images' pixels freed once no frame in flight reads them) */
	xgpu_images_frame_end();
	xgpu_texture_cache_begin_frame();
#endif
#ifndef XGPU_STREAM_TEST
	/* (the frames paced by display.max_fps while the host shows this one:
	sdl_platform.c; with vsync on, the Metal host's display paces them
	instead, frame_pacing.h) */
	platform_video_swap();
#endif
}

/* ---------- enhanced dynamic lights */

BOOL xgpu_backend_dynamic_lights_prepare(void)
{
	if (!stream.open || !stream.capabilities.dynamic_lights_preparable)
		return FALSE;
	record(XGPU_STREAM_DYNAMIC_LIGHTS_PREPARE, sizeof(struct xgpu_stream_record));
	return TRUE;
}

#define DYNAMIC_LIGHTS_CELLS_SIZE \
	(XGPU_DYNAMIC_LIGHTS_GRID_X * XGPU_DYNAMIC_LIGHTS_GRID_Y * XGPU_DYNAMIC_LIGHTS_GRID_Z * 16)

void xgpu_backend_dynamic_lights_upload(const struct xgpu_dynamic_lights_block *block, const void *cells)
{
	static const float zero[4];
	struct xgpu_stream_dynamic_lights_upload *record_body;
	uint32_t count = XGPU_DYNAMIC_LIGHTS_MAXIMUM;
	uint32_t light;
	unsigned char *place;

	if (!stream.open)
		return;
	/* the lights past the last that is there are zeros */
	while (count && !memcmp(block->position[count - 1], zero, 16) && !memcmp(block->color[count - 1], zero, 16) &&
		!memcmp(block->cone[count - 1], zero, 16))
		count--;
	record_body = record_data(XGPU_STREAM_DYNAMIC_LIGHTS_UPLOAD, sizeof(*record_body),
		carried_one(DYNAMIC_LIGHTS_CELLS_SIZE), &place);
	record_body->count = count;
	record_body->size = DYNAMIC_LIGHTS_CELLS_SIZE;
	memcpy(record_body->block.grid, block->grid, sizeof(record_body->block.grid));
	for (light = 0; light < count; light++)
	{
		memcpy(record_body->block.position[light], block->position[light], 16);
		memcpy(record_body->block.color[light], block->color[light], 16);
		memcpy(record_body->block.cone[light], block->cone[light], 16);
	}
	stream_finite(&record_body->block.grid[0][0], sizeof(record_body->block.grid) / 4, "dynamic lights' grid", 0);
	stream_finite(&record_body->block.position[0][0], count * 4, "dynamic light place", 0);
	stream_finite(&record_body->block.color[0][0], count * 4, "dynamic light colour", 0);
	stream_finite(&record_body->block.cone[0][0], count * 4, "dynamic light cone", 0);
	record_body->cells = carry_one(&place, cells, DYNAMIC_LIGHTS_CELLS_SIZE);
}

void xgpu_backend_dynamic_lights_halos(xgpu_target color, xgpu_target depth, int layers,
	const struct xgpu_dynamic_lights_halos *halos)
{
	struct xgpu_stream_dynamic_lights_halos *record_body;
	int count = halos->count;

	if (!stream.open)
		return;
	if (count < 0)
		count = 0;
	if (count > XGPU_DYNAMIC_LIGHTS_MAXIMUM)
		count = XGPU_DYNAMIC_LIGHTS_MAXIMUM;
	record_body = record(XGPU_STREAM_DYNAMIC_LIGHTS_HALOS, sizeof(*record_body) + count * 2 * sizeof(record_body->halos[0]));
	record_body->color = color;
	record_body->depth = depth;
	record_body->layers = layers;
	record_body->count = count;
	memcpy(record_body->camera_right, halos->camera_right, sizeof(record_body->camera_right));
	memcpy(record_body->camera_up, halos->camera_up, sizeof(record_body->camera_up));
	memcpy(record_body->projection, halos->projection, sizeof(record_body->projection));
	memcpy(record_body->viewport_scale, halos->viewport_scale, sizeof(record_body->viewport_scale));
	memcpy(record_body->viewport_offset, halos->viewport_offset, sizeof(record_body->viewport_offset));
	memcpy(record_body->camera, halos->camera, sizeof(record_body->camera));
	record_body->softness = halos->softness;
	record_body->screen_offset = halos->screen_offset;
	memcpy(record_body->halos, halos->halos, count * 2 * sizeof(record_body->halos[0]));
	stream_finite(&record_body->halos[0][0], (unsigned long)count * 2 * 4, "dynamic light glow value", 0);
}

/* ---------- the VR mode */

BOOL xgpu_backend_vr_supported(void)
{
	struct xgpu_stream_reply_record *record_body;

	if (!stream.open)
		return FALSE;
	record_body = record(XGPU_STREAM_VR_SUPPORTED, sizeof(*record_body));
	record_body->reply = STREAM_ADDRESS(&stream.replies->value);
	stream.replies->value = 0;
	synchronous();
	return stream.replies->value != 0;
}

void xgpu_backend_prepare(void)
{
	if (!stream.open)
		return;
	record(XGPU_STREAM_PREPARE, sizeof(struct xgpu_stream_record));
	stream.preparing = TRUE;
}

void xgpu_backend_frame_extra(unsigned long microseconds)
{
	frame_extra_microseconds = (uint32_t)microseconds;
	frame_extra_frames = microseconds ? XGPU_FRAME_EXTRA_FRAMES : 0;
}

void xgpu_backend_loading_art(const unsigned char *rgba)
{
	struct xgpu_stream_value *body;
	uint32_t bytes = rgba ? XGPU_STREAM_LOADING_ART_BYTES : 0;

	if (!stream.open)
		return;
	body = record(XGPU_STREAM_LOADING_ART, sizeof(*body) + bytes);
	body->value = bytes;
	if (bytes)
		memcpy(body + 1, rgba, bytes);
	synchronous();
}

void xgpu_backend_loading_scene(const void *data, unsigned long bytes)
{
	struct xgpu_stream_value *body;

	if (!stream.open || bytes > XGPU_STREAM_RECORD_MAXIMUM - sizeof(*body) - 64)
		return;
	body = record(XGPU_STREAM_LOADING_SCENE, sizeof(*body) + (uint32_t)bytes);
	body->value = (uint32_t)bytes;
	if (bytes)
		memcpy(body + 1, data, bytes);
}

void xgpu_backend_loading_progress(float progress)
{
	uint32_t bits;

	if (!stream.open)
		return;
	memcpy(&bits, &progress, sizeof(bits));
	record_value(XGPU_STREAM_LOADING_PROGRESS, bits);
}

void xgpu_backend_loading(int active)
{
	if (!stream.open)
		return;
	record_value(XGPU_STREAM_LOADING, (uint32_t)active);
	/* Readiness travels with the first real picture. Publishing it alone
	would stop loading presentation while the game builds that picture. */
	if (active != 2)
		synchronous();
}

int xgpu_backend_loading_owned(void)
{
	struct xgpu_stream_reply_record *record_body;

	if (!stream.open)
		return 0;
	record_body = record(XGPU_STREAM_LOADING_OWNED, sizeof(*record_body));
	record_body->reply = STREAM_ADDRESS(&stream.replies->value);
	stream.replies->value = 0;
	synchronous();
	return stream.replies->value != 0;
}

double xgpu_backend_prepared(void)
{
	struct xgpu_stream_reply_record *record_body;

	if (!stream.open)
		return 0.0;
	stream.preparing = FALSE;
	record_body = record(XGPU_STREAM_PREPARED, sizeof(*record_body));
	record_body->reply = STREAM_ADDRESS(&stream.replies->value);
	stream.replies->value = 0;
	synchronous();
	return stream.replies->value / 1000.0;
}

void xgpu_backend_vr_initialize(struct xgpu_vr_info *info)
{
	struct xgpu_stream_reply_record *record_body;
	struct xgpu_stream_vr_info *wire = &stream.replies->vr_info;

	if (!stream.open)
		return;
	wire->eye_width = info->eye_width;
	wire->eye_height = info->eye_height;
	wire->hud_width = info->hud_width;
	wire->hud_height = info->hud_height;
	wire->scope_size = info->scope_size;
	wire->statistics = (info->statistics ? XGPU_STREAM_VR_STATISTICS : 0) |
		(info->gpu_time ? XGPU_STREAM_VR_GPU_TIME : 0);
	wire->foveated = info->foveated;
	wire->fovea_width = info->fovea_width;
	wire->fovea_height = info->fovea_height;
	wire->sample_counting = xgpu_backend_capabilities.sample_counting;
#ifdef HALO_VR
	wire->foveation_inset = halo_vr_foveation_inset();
	snprintf(wire->foveation_name, sizeof(wire->foveation_name), "%s", halo_vr_foveation_name());
#else
	wire->foveation_inset = 0.0f;
	wire->foveation_name[0] = 0;
#endif
	record_body = record(XGPU_STREAM_VR_INITIALIZE, sizeof(*record_body));
	record_body->reply = STREAM_ADDRESS(wire);
	synchronous();
	info->foveated = wire->foveated;
	info->fovea_width = wire->fovea_width;
	info->fovea_height = wire->fovea_height;
	xgpu_backend_capabilities.sample_counting = wire->sample_counting != 0;
}

void xgpu_backend_vr_eyes(xgpu_target eye_target, unsigned int image)
{
	struct xgpu_stream_vr_eyes *record_body;

	if (!stream.open)
		return;
	record_body = record(XGPU_STREAM_VR_EYES, sizeof(*record_body));
	record_body->eye_target = eye_target;
	record_body->image = image;
	record_body->foveation = 0;
	record_body->latched = 0;
#ifdef HALO_VR
	/* (where the backend asks, foveated: host_vulkan_vr.c's fovea_place) */
	if (xgpu_foveated)
		record_body->foveation = halo_vr_foveation(record_body->centres);
	{
		struct vr_host_latch latch;

		if (halo_vr_latch(&latch))
		{
			record_body->latched = 1;
			memcpy(record_body->latch, &latch, sizeof(latch));
		}
	}
#endif
}

void xgpu_backend_vr_pass(int pass)
{
	if (stream.open)
		record_value(XGPU_STREAM_VR_PASS, (uint32_t)pass);
}

void xgpu_backend_vr_groups_reset(void)
{
	if (stream.open)
		record(XGPU_STREAM_VR_GROUPS_RESET, sizeof(struct xgpu_stream_record));
}

void xgpu_backend_vr_timing(int mark)
{
	if (stream.open)
		record_value(XGPU_STREAM_VR_TIMING, (uint32_t)mark);
}

/* (VR_SUBMIT's: the rest is the caller's) */
static struct xgpu_stream_vr_frame_record *vr_frame_record(uint32_t type, const struct xgpu_vr_frame *frame,
	BOOL screenshot)
{
	struct xgpu_stream_vr_frame_record *record_body;

	if (!stream.open)
		return NULL;
	record_body = record(type, type == XGPU_STREAM_VR_SUBMIT ? sizeof(struct xgpu_stream_vr_submit) : sizeof(*record_body));
	record_body->frame.eyes_drawn = frame->eyes_drawn;
	record_body->frame.hud_drawn = frame->hud_drawn;
	record_body->frame.scope_drawn = frame->scope_drawn;
	record_body->frame.hud_panels = frame->hud_panels;
	record_body->frame.eye_target = frame->eye_target;
	record_body->frame.eye_depth = frame->eye_depth;
	record_body->frame.scope = frame->scope;
	record_body->frame.hud = frame->hud;
	record_body->frame.eye_depth_layers = frame->eye_depth_layers;
	record_body->screenshot = screenshot ? 1 : 0;
	return record_body;
}

#ifdef HALO_VR
_Static_assert(sizeof(struct vr_host_layers) == XGPU_STREAM_VR_LAYERS_WORDS * 4, "VR layers");
_Static_assert(sizeof(struct vr_host_pose) == XGPU_STREAM_VR_POSE_WORDS * 4, "VR pose");
_Static_assert(sizeof(struct halo_vr_scope_window) == XGPU_STREAM_VR_SCOPE_WINDOW_WORDS * 4, "VR scope window");
_Static_assert(sizeof(struct halo_vr_cockpit_window) == XGPU_STREAM_VR_COCKPIT_WINDOW_WORDS * 4, "VR cockpit window");
_Static_assert(sizeof(struct halo_vr_cockpit_display) == XGPU_STREAM_VR_COCKPIT_DISPLAY_WORDS * 4,
	"VR cockpit display");
_Static_assert(sizeof(struct vr_visor_hud_image) == XGPU_STREAM_VR_HUD_IMAGE_WORDS * 4, "VR HUD image");
_Static_assert(sizeof(struct vr_visor_panel) == XGPU_STREAM_VR_PANEL_WORDS * 4, "VR panel");
_Static_assert(offsetof(struct vr_visor_image, noise_texture) == XGPU_STREAM_VR_VISOR_IMAGE_WORDS * 4, "VR visor image");
_Static_assert(sizeof(struct vr_visor_rim) == XGPU_STREAM_VR_RIM_WORDS * 4, "VR rim");

/* what the composition will ask of vr.c and vr_visor.c, as they answer now
(xgpu_stream.h's VR_STATE) */
static void vr_state_record(void)
{
	struct xgpu_stream_vr_state *record_body = record(XGPU_STREAM_VR_STATE, sizeof(*record_body));
	struct halo_vr_scope_window window;
	struct halo_vr_cockpit_window cockpit;
	struct halo_vr_cockpit_display display;
	struct vr_visor_hud_image hud;
	struct vr_visor_panel panels[VR_PANEL_COUNT];
	struct vr_visor_image visor;
	struct vr_visor_rim rim;
	int loaded = 0, carried = 0;

	memset((unsigned char *)record_body + sizeof(record_body->header), 0, sizeof(*record_body) - sizeof(record_body->header));
	record_body->scope_zoom = halo_vr_scope_zoom(&record_body->scope_zoom_height);
	if (halo_vr_scope_smartlink(record_body->scope_reticle))
		record_body->answered |= XGPU_STREAM_VR_STATE_SMARTLINK;
	if (halo_vr_scope_window(&window))
	{
		record_body->answered |= XGPU_STREAM_VR_STATE_SCOPE_WINDOW;
		memcpy(record_body->scope_window, &window, sizeof(window));
	}
	halo_vr_scope_readout(&record_body->scope_range, &loaded, &carried);
	record_body->scope_rounds_loaded = loaded;
	record_body->scope_rounds_carried = carried;
	halo_vr_scope_animation(&record_body->scope_animation[0], &record_body->scope_animation[1],
		&record_body->scope_animation[2]);
	halo_vr_scope_shape(&record_body->scope_shape[0], &record_body->scope_shape[1], &record_body->scope_shape[2]);
	record_body->scope_gun = halo_vr_scope_gun();
	if (halo_vr_cockpit_window(&cockpit, &display))
	{
		record_body->answered |= XGPU_STREAM_VR_STATE_COCKPIT;
		memcpy(record_body->cockpit_window, &cockpit, sizeof(cockpit));
		memcpy(record_body->cockpit_display, &display, sizeof(display));
	}
	record_body->vignette = halo_vr_vignette(record_body->vignette_fov);
	vr_visor_hud_image(&hud);
	memcpy(record_body->hud_image, &hud, sizeof(hud));
	if (vr_visor_panels(panels))
	{
		record_body->answered |= XGPU_STREAM_VR_STATE_PANELS;
		memcpy(record_body->panels, panels, sizeof(panels));
	}
	vr_visor_image(&visor);
	memcpy(record_body->visor_image, &visor, sizeof(record_body->visor_image));
	record_body->visor_noise[0] = STREAM_ADDRESS(visor.noise_texture[0]);
	record_body->visor_noise[1] = STREAM_ADDRESS(visor.noise_texture[1]);
	record_body->visor_time = visor.time;
	if (vr_visor_rim(&rim))
	{
		record_body->answered |= XGPU_STREAM_VR_STATE_RIM;
		memcpy(record_body->rim, &rim, sizeof(rim));
	}
	if (halo_vr_depth_wanted())
		record_body->answered |= XGPU_STREAM_VR_STATE_DEPTH;
	if (halo_vr_frame_waited())
		record_body->answered |= XGPU_STREAM_VR_STATE_FRAME;
}
#endif

void xgpu_backend_vr_compose(const struct xgpu_vr_frame *frame)
{
#ifdef HALO_VR
	if (stream.open)
		vr_state_record();
#endif
	vr_frame_record(XGPU_STREAM_VR_COMPOSE, frame, FALSE);
}

void xgpu_backend_vr_submit(const struct xgpu_vr_frame *frame, BOOL screenshot)
{
	struct xgpu_stream_vr_submit *submit;
#ifdef HALO_VR
	struct vr_host_layers layers;
	struct vr_host_pose head;
#endif

	if (!stream.open)
		return;
	/* (the layers made as the guest's backend has them made as it submits,
	every image taken as acquired: the host leaves out those it has not) */
#ifdef HALO_VR
	halo_vr_present(frame->eyes_drawn, frame->hud_drawn, frame->scope_drawn);
#endif
	submit = (struct xgpu_stream_vr_submit *)vr_frame_record(XGPU_STREAM_VR_SUBMIT, frame, screenshot);
	submit->waited = 0;
#ifdef XGPU_STREAM_TEST
	submit->shot = 0;
#else
	submit->shot = (uint32_t)xgpu_screenshot_number();
#endif
	memset(submit->layers, 0, sizeof(submit->layers));
	memset(submit->head, 0, sizeof(submit->head));
#ifdef HALO_VR
	if (halo_vr_submitted(&layers, &head))
	{
		submit->waited = 1;
		memcpy(submit->layers, &layers, sizeof(layers));
		memcpy(submit->head, &head, sizeof(head));
	}
#endif
}

#ifdef HALO_VR
/* (vr.h: what vr.c sets in the backend as it draws, in its place among the
records) */
static void vr_values_record(uint32_t type, const float *values, uint32_t count)
{
	struct xgpu_stream_vr_values *record_body;

	if (!stream.open)
		return;
	record_body = record(type, (uint32_t)(sizeof(*record_body) + count * sizeof(float)));
	memcpy(record_body->values, values, count * sizeof(float));
}

_Static_assert(sizeof(struct vr_host_latch) == XGPU_STREAM_VR_LATCH_WORDS * 4, "VR latch");

/* the late latch's for the next eye transforms and screen corrections
(halo_vr_eye_latch, halo_vr_screen_point) */
static struct
{
	int latch;
	float clip_from_eye[2][16];
	float point[4];
	float viewport[2];
} vr_latch;

void halo_vr_eye_latch(int latch, const float clip_from_eye[2][16])
{
	vr_latch.latch = latch;
	memcpy(vr_latch.clip_from_eye, clip_from_eye, sizeof(vr_latch.clip_from_eye));
}

void halo_vr_screen_point(const float clip[4], float width, float height)
{
	memcpy(vr_latch.point, clip, sizeof(vr_latch.point));
	vr_latch.viewport[0] = width;
	vr_latch.viewport[1] = height;
}

void halo_vr_set_eye_transforms(const float transforms[2][16])
{
	struct xgpu_stream_vr_eye_transforms *record_body;

	if (!stream.open)
		return;
	record_body = record(XGPU_STREAM_VR_EYE_TRANSFORMS, sizeof(*record_body));
	record_body->latch = vr_latch.latch;
	memcpy(record_body->transforms, transforms, sizeof(record_body->transforms));
	memcpy(record_body->clip_from_eye, vr_latch.clip_from_eye, sizeof(record_body->clip_from_eye));
	vr_latch.latch = XGPU_STREAM_VR_LATCH_NONE;
}

void halo_vr_set_screen_corrections(const float corrections[2][4])
{
	struct xgpu_stream_vr_screen_corrections *record_body;

	if (!stream.open)
		return;
	record_body = record(XGPU_STREAM_VR_SCREEN_CORRECTIONS, sizeof(*record_body));
	memcpy(record_body->corrections, corrections, sizeof(record_body->corrections));
	memcpy(record_body->point, vr_latch.point, sizeof(record_body->point));
	memcpy(record_body->viewport, vr_latch.viewport, sizeof(record_body->viewport));
	memset(vr_latch.point, 0, sizeof(vr_latch.point));
}

void halo_vr_sky(int sky)
{
	halo_gpu_preparation_sky(sky);
	if (stream.open)
		record_value(XGPU_STREAM_VR_SKY, (uint32_t)sky);
}

void halo_vr_eye_feed(float squash, float shrink, float bright, float noise, float red, float look, float interference)
{
	const float feed[7] = { squash, shrink, bright, noise, red, look, interference };

	vr_values_record(XGPU_STREAM_VR_EYE_FEED, feed, 7);
}

void halo_vr_eye_tint(float red, float green, float blue)
{
	const float tint[3] = { red, green, blue };

	vr_values_record(XGPU_STREAM_VR_EYE_TINT, tint, 3);
}
#endif

