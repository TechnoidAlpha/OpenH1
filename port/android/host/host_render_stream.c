/*
HOST_RENDER_STREAM.C

The renderer's command stream, the host's half (host_render_stream.h,
port/linux/src/xgpu_stream.h): the decoder that plays the guest's records
into a backend, the render thread it runs on, and the guest's imports that
hand it the frames.

The guest submits records up to its ring header's committed; the render
thread plays them in order. After each frame's last record, and whenever
the guest waits for room or for a synchronous record, it asks the backend
for a fence; with nothing left to play it waits for the oldest fence and
releases the ring's bytes up to it, retiring the frames it ends. The game
so waits only when the ring is full, more than frames_in_flight frames are
unretired, or a record is synchronous: the GPU draws frame N while this
thread plays N + 1 and the game writes N + 2.

A backend that shows the frames on a display paces the game by it
(frame_pacing.h): the game starts a frame only while at most
FRAME_PACING_QUEUED of its frames wait for the display, and not before the
budget a frame takes ahead of the display's latch it is for. Each frame's
FRAME_END says when its input was read, for the time from then to the
frame on screen.
*/

#include "host.h"
#include "host_load_trace.h"
#include "host_render_record.h"
#include "host_render_stream.h"
#include "frame_pacing.h"
#ifdef HALO_VR
#include "host_vr_render.h"
#endif

#ifdef __APPLE__
/* (Mach's host_page_size is another than host.h's) */
#define host_page_size host_page_size_of_mach
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#undef host_page_size
#include <dlfcn.h>
#endif

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------- the decoder */

static uint64_t monotonic_nanoseconds(void);
static struct
{
	const struct host_render_backend *backend;
	struct loading_presentation_frame frame;
	uint64_t next;
	uint64_t costs[8];
	unsigned cost_index;
	/* when it last drew, for the longest it may wait for the stream */
	uint64_t drawn;
	int servicing, ready;
} loading;

/* The loading picture covers the stream's work while the game waits for
it, so it never makes the game wait: it is drawn when the guest waits for
nothing the render thread has yet to do and the stream is played to its
end, or once it has waited LOADING_DEFERRED_MOST_NS. Drawn on the thread
that plays the stream, a picture holds it as long as the display or the VR
runtime makes it wait: on the Steam Frame, 12 ms of its 13.9 ms period in
xrBeginFrame and xrEndFrame, which left the stream 1.5 ms of each, and
Blood Gulch's enumeration (129 MB through the 32 MB ring) took 5.3 s; so,
1.1 s. */
#define LOADING_DEFERRED_MOST_NS 100000000ull

static int loading_stream_waits(void);

int host_render_stream_loading_holds(void)
{
	return loading.frame.active && !loading.ready;
}

void host_render_stream_loading_service(void)
{
	uint64_t now, interval, completed, maximum, budget, slack, margin;
	unsigned index;

	if (!loading.frame.active || loading.ready || loading.servicing || !loading.backend || !loading.backend->loading)
		return;
	now = monotonic_nanoseconds();
	if (now < loading.next)
		return;
	if (loading_stream_waits() && now - loading.drawn < LOADING_DEFERRED_MOST_NS)
		return;
	loading.servicing = 1;
	loading.frame.elapsed_ns = now - loading.frame.started_ns;
	loading.frame.serial++;
	loading_presentation_build(&loading.frame);
	interval = loading.backend->loading(loading.backend->context, &loading.frame);
	if (!interval)
		interval = 16666667ull;
	loading.next = now + interval;
	completed = monotonic_nanoseconds();
	loading.drawn = completed;
	loading.costs[loading.cost_index++ % 8] = completed - now;
	/* A late callback must leave time for queued work. Spend the spare
	time before twice its requested service interval, using recent costs
	to allow for swap jitter. OpenXR requests 1 ms ticket polls here;
	that interval is not the headset's refresh period. */
	if (loading.next <= completed)
	{
		maximum = 0;
		for (index = 0; index < 8; index++)
			if (loading.costs[index] > maximum)
				maximum = loading.costs[index];
		budget = interval / 2;
		/* Leave a quarter interval for a slower swap than recent samples.
		The desktop driver's measured variation exceeds a millisecond. */
		margin = interval / 4;
		if (margin < 1000000ull)
			margin = 1000000ull;
		slack = interval * 2 > maximum + margin ? interval * 2 - maximum - margin : 0;
		if (budget > slack)
			budget = slack;
		/* Even a presenter slower than two intervals must not starve
		preparation. It cannot meet that cadence on this thread. */
		if (budget < 1000000ull)
			budget = 1000000ull;
		loading.next = completed + budget;
	}
	loading.servicing = 0;
}

static void loading_set(const struct host_render_backend *backend, int active)
{
	if (active == 2)
	{
		loading.ready = 1;
		loading.frame.ready = 1;
		host_load_trace("loading.ready", "epoch=%llu serial=%llu",
			(unsigned long long)loading.frame.epoch, (unsigned long long)loading.frame.serial);
		if (backend->loading)
			backend->loading(backend->context, &loading.frame);
#ifdef HALO_VR
		host_vr_loading_set(2);
#endif
		return;
	}
	if (active == (int)loading.frame.active)
		return;
	host_load_trace("loading.received", "active=%d", active);
	loading.backend = backend;
	loading.frame.active = active;
	loading.ready = 0;
	loading.frame.ready = 0;
	if (active)
	{
		loading.frame.epoch++;
		loading.frame.serial = 0;
		memset(loading.costs, 0, sizeof(loading.costs));
		loading.cost_index = 0;
		loading.frame.started_ns = monotonic_nanoseconds();
		loading.next = 0;
	}
#ifdef HALO_VR
	host_vr_loading_set(active);
#endif
	host_load_trace(active ? "loading.begin" : "loading.end", "epoch=%llu serial=%llu",
		(unsigned long long)loading.frame.epoch, (unsigned long long)loading.frame.serial);
	if (active)
		host_render_stream_loading_service();
	else if (backend->loading)
		backend->loading(backend->context, &loading.frame);
}

struct host_render_decoder
{
	const struct host_render_backend *backend;
	void *context;
	struct xgpu_stream_ring *ring;
	unsigned char *buffer;
	uint32_t size, mask;
	volatile uint8_t *pipeline_states;

	struct host_render_texture_stage stages[4];
	struct host_render_vertex_input input;
	float immediate_attributes[16][4];
	struct xgpu_stream_dynamic_state dynamic;
	struct xgpu_stream_draw_uniforms uniforms;
	uint32_t uniforms_serial;
	struct xgpu_stream_pipeline_define *pipelines[XGPU_STREAM_PIPELINES];
	struct xgpu_stream_layout_define *layouts[XGPU_STREAM_LAYOUTS];

	/* the records played reached a frame's end */
	int frame_ended;
	/* the frame being ended's input time (FRAME_END's; 0 unknown) */
	uint64_t frame_input;
	int trace_preparing;
	struct { uint64_t count; double seconds, maximum; } trace_operations[XGPU_STREAM_RECORD_TYPES];
};

/* the least size of each type's record */
static const uint32_t record_minimum[XGPU_STREAM_RECORD_TYPES] =
{
	[XGPU_STREAM_LOADING] = sizeof(struct xgpu_stream_value),
	[XGPU_STREAM_LOADING_OWNED] = sizeof(struct xgpu_stream_reply_record),
	[XGPU_STREAM_LOADING_ART] = sizeof(struct xgpu_stream_value),
	[XGPU_STREAM_LOADING_SCENE] = sizeof(struct xgpu_stream_value),
	[XGPU_STREAM_LOADING_PROGRESS] = sizeof(struct xgpu_stream_value),
	[XGPU_STREAM_SKIP] = sizeof(struct xgpu_stream_record),
	[XGPU_STREAM_INITIALIZE] = sizeof(struct xgpu_stream_reply_record),
	[XGPU_STREAM_INVALIDATE] = sizeof(struct xgpu_stream_record),
	[XGPU_STREAM_FLUSH] = sizeof(struct xgpu_stream_record),
	[XGPU_STREAM_ANTI_ALIASING_PREPARE] = sizeof(struct xgpu_stream_value),
	[XGPU_STREAM_ANTI_ALIAS] = sizeof(struct xgpu_stream_anti_alias),
	[XGPU_STREAM_HDR] = sizeof(struct xgpu_stream_hdr),
	[XGPU_STREAM_TONE_MAP] = sizeof(struct xgpu_stream_tone_map),
	[XGPU_STREAM_AMBIENT_OCCLUSION] = sizeof(struct xgpu_stream_ambient_occlusion),
	[XGPU_STREAM_PICTURE_POSITION] = sizeof(struct xgpu_stream_value),
	[XGPU_STREAM_GAMMA] = sizeof(struct xgpu_stream_value),
	/* (and its folder's path, at least its NUL) */
	[XGPU_STREAM_SCREENSHOT] = sizeof(struct xgpu_stream_screenshot) + XGPU_STREAM_RECORD_ALIGNMENT,
	[XGPU_STREAM_TARGET_CREATE] = sizeof(struct xgpu_stream_target_create),
	[XGPU_STREAM_TARGET_DELETE] = sizeof(struct xgpu_stream_value),
	[XGPU_STREAM_TARGET_MULTISAMPLE] = sizeof(struct xgpu_stream_target_multisample),
	[XGPU_STREAM_TARGET_RESOLVE] = sizeof(struct xgpu_stream_value),
	[XGPU_STREAM_SET_TARGETS] = sizeof(struct xgpu_stream_set_targets),
	[XGPU_STREAM_CLEAR] = sizeof(struct xgpu_stream_clear),
	[XGPU_STREAM_CLEAR_TARGET] = sizeof(struct xgpu_stream_clear_target),
	[XGPU_STREAM_READ_TARGET] = sizeof(struct xgpu_stream_read_target),
	[XGPU_STREAM_SET_TEXTURES] = sizeof(struct xgpu_stream_set_textures),
	[XGPU_STREAM_CONSTANTS] = sizeof(struct xgpu_stream_constants),
	[XGPU_STREAM_VERTEX_PROGRAM] = sizeof(struct xgpu_stream_vertex_program),
	[XGPU_STREAM_VERTEX_LIGHTING] = sizeof(struct xgpu_stream_vertex_lighting),
	[XGPU_STREAM_PIPELINE_DEFINE] = sizeof(struct xgpu_stream_pipeline_define),
	[XGPU_STREAM_PIPELINE] = sizeof(struct xgpu_stream_value),
	[XGPU_STREAM_DRAW_STATE] = sizeof(struct xgpu_stream_draw_state),
	[XGPU_STREAM_MIRROR_WRITE] = sizeof(struct xgpu_stream_mirror_write),
	[XGPU_STREAM_LAYOUT_DEFINE] = sizeof(struct xgpu_stream_layout_define),
	[XGPU_STREAM_VERTICES] = sizeof(struct xgpu_stream_vertices),
	[XGPU_STREAM_DRAW] = sizeof(struct xgpu_stream_draw),
	[XGPU_STREAM_DRAW_INDICES] = sizeof(struct xgpu_stream_draw_indices),
	[XGPU_STREAM_DRAW_INDEXED] = sizeof(struct xgpu_stream_draw_indexed),
	[XGPU_STREAM_DRAW_IMMEDIATE] = sizeof(struct xgpu_stream_draw_immediate),
	[XGPU_STREAM_VISIBILITY_BEGIN] = sizeof(struct xgpu_stream_record),
	[XGPU_STREAM_VISIBILITY_END] = sizeof(struct xgpu_stream_visibility_end),
	[XGPU_STREAM_VISIBILITY_FRAME] = sizeof(struct xgpu_stream_record),
	[XGPU_STREAM_PRESENT] = sizeof(struct xgpu_stream_value),
	[XGPU_STREAM_FRAME_END] = sizeof(struct xgpu_stream_record),
	[XGPU_STREAM_DYNAMIC_LIGHTS_PREPARE] = sizeof(struct xgpu_stream_record),
	[XGPU_STREAM_DYNAMIC_LIGHTS_UPLOAD] = sizeof(struct xgpu_stream_dynamic_lights_upload),
	[XGPU_STREAM_DYNAMIC_LIGHTS_HALOS] = sizeof(struct xgpu_stream_dynamic_lights_halos),
	[XGPU_STREAM_VR_SUPPORTED] = sizeof(struct xgpu_stream_reply_record),
	[XGPU_STREAM_PREPARED] = sizeof(struct xgpu_stream_reply_record),
	[XGPU_STREAM_PREPARE] = sizeof(struct xgpu_stream_record),
	[XGPU_STREAM_VR_INITIALIZE] = sizeof(struct xgpu_stream_reply_record),
	[XGPU_STREAM_VR_EYES] = sizeof(struct xgpu_stream_vr_eyes),
	[XGPU_STREAM_VR_PASS] = sizeof(struct xgpu_stream_value),
	[XGPU_STREAM_VR_GROUPS_RESET] = sizeof(struct xgpu_stream_record),
	[XGPU_STREAM_VR_TIMING] = sizeof(struct xgpu_stream_value),
	[XGPU_STREAM_VR_COMPOSE] = sizeof(struct xgpu_stream_vr_frame_record),
	[XGPU_STREAM_VR_SUBMIT] = sizeof(struct xgpu_stream_vr_submit),
	[XGPU_STREAM_VR_STATE] = sizeof(struct xgpu_stream_vr_state),
	[XGPU_STREAM_VR_EYE_TRANSFORMS] = sizeof(struct xgpu_stream_vr_eye_transforms),
	[XGPU_STREAM_VR_SCREEN_CORRECTIONS] = sizeof(struct xgpu_stream_vr_screen_corrections),
	[XGPU_STREAM_VR_SKY] = sizeof(struct xgpu_stream_value),
	[XGPU_STREAM_VR_EYE_FEED] = sizeof(struct xgpu_stream_vr_values) + 7 * sizeof(float),
	[XGPU_STREAM_VR_EYE_TINT] = sizeof(struct xgpu_stream_vr_values) + 3 * sizeof(float),
};

struct host_render_decoder *host_render_decoder_new(const struct host_render_backend *backend,
	struct xgpu_stream_ring *ring)
{
	struct host_render_decoder *decoder = calloc(1, sizeof(*decoder));

	if (!decoder)
		return NULL;
	decoder->backend = backend;
	decoder->context = backend->context;
	decoder->ring = ring;
	decoder->buffer = host_pointer(ring->buffer);
	decoder->size = ring->size;
	decoder->mask = ring->size - 1;
	decoder->pipeline_states = host_pointer(ring->pipelines);
	return decoder;
}

void host_render_decoder_free(struct host_render_decoder *decoder)
{
	uint32_t index;

	if (!decoder)
		return;
	for (index = 0; index < XGPU_STREAM_PIPELINES; index++)
		free(decoder->pipelines[index]);
	for (index = 0; index < XGPU_STREAM_LAYOUTS; index++)
		free(decoder->layouts[index]);
	free(decoder);
}

/* data a record names, bytes of it at address: in place anywhere in the
guest's memory, or if in the ring, inside the record */
static int data_inside(const struct host_render_decoder *decoder, const void *record, uint32_t record_size,
	uint32_t address, uint32_t bytes)
{
	uint32_t buffer = decoder->ring->buffer;
	uint32_t start = guest_address(record);

	if (!bytes)
		return 1;
	if (!address)
		return 0;
	if (address - buffer >= decoder->size)
		return address + bytes >= address;
	return address >= start && address - start <= record_size && bytes <= record_size - (address - start);
}

/* HALO_LOADING_SCENE_DUMP=<file>: the main menu's scene as it arrives
(loading_scene.h), for looking at the loading screen without the game */
static void host_loading_scene_dump(const void *data, uint32_t bytes)
{
	static int checked;
	static const char *path;
	FILE *file;

	if (!checked)
	{
		path = getenv("HALO_LOADING_SCENE_DUMP");
		checked = 1;
	}
	if (!path || !*path || !bytes)
		return;
	file = fopen(path, "wb");
	if (!file)
		return;
	fwrite(data, 1, bytes, file);
	fclose(file);
}

static void answer(uint32_t reply, const void *value, size_t size)
{
	if (reply)
		memcpy(host_pointer(reply), value, size);
}

static host_render_screenshot_writer screenshot_writer;

void host_render_stream_set_screenshot_writer(host_render_screenshot_writer writer)
{
	screenshot_writer = writer;
}

/* XGPU_STREAM_SCREENSHOT: the target read as READ_TARGET reads it, tone
mapped, into the guest's pixels, and written; 1 if written */
static uint32_t screenshot(const struct host_render_backend *backend, void *context,
	const struct xgpu_stream_screenshot *shot, uint32_t size)
{
	struct xgpu_stream_read_target read;
	uint32_t length = size - (uint32_t)sizeof(*shot), written;
	char *name;

	/* (the folder's path ends within the record) */
	if (!memchr(shot->directory, 0, length) || !shot->directory[0])
		return 0;
	if (!backend->read_target || !screenshot_writer || !shot->pixels || !shot->name || !shot->width || !shot->height ||
		(uint64_t)shot->width * shot->height * 4 > shot->size)
		return 0;
	memset(&read, 0, sizeof(read));
	read.header = XGPU_STREAM_HEADER(XGPU_STREAM_READ_TARGET, sizeof(read));
	read.target = shot->target;
	read.layer = -1;
	read.tone_map = 1;
	read.pixels = shot->pixels;
	read.size = shot->size;
	name = host_pointer(shot->name);
	if (!backend->read_target(context, &read))
	{
		snprintf(name, XGPU_STREAM_SCREENSHOT_NAME, "the frame cannot be read");
		return 0;
	}
	name[0] = 0;
	written = screenshot_writer(host_pointer(shot->pixels), shot->width, shot->height, shot->directory, name,
		XGPU_STREAM_SCREENSHOT_NAME) ? 1 : 0;
	name[XGPU_STREAM_SCREENSHOT_NAME - 1] = 0;
	return written;
}

#define CALL(name, ...) do { if (backend->name) backend->name(context, __VA_ARGS__); } while (0)
#define CALL0(name) do { if (backend->name) backend->name(context); } while (0)

/* plays one record; 0, or an XGPU_STREAM_ERROR_* */
static int decode_record(struct host_render_decoder *decoder, const unsigned char *start, uint32_t type, uint32_t size)
{
	const struct host_render_backend *backend = decoder->backend;
	void *context = decoder->context;
	const struct xgpu_stream_value *value = (const void *)start;

	switch (type)
	{
	case XGPU_STREAM_LOADING_ART:
		if ((value->value != 0 && value->value != XGPU_STREAM_LOADING_ART_BYTES) ||
			size - sizeof(*value) != value->value)
			return XGPU_STREAM_ERROR_RECORD;
		CALL(loading_art, value->value ? start + sizeof(*value) : NULL);
		break;
	case XGPU_STREAM_LOADING_SCENE:
		if (size - sizeof(*value) != value->value)
			return XGPU_STREAM_ERROR_RECORD;
		host_loading_scene_dump(start + sizeof(*value), value->value);
		CALL(loading_scene, start + sizeof(*value), value->value);
		break;
	case XGPU_STREAM_LOADING_PROGRESS:
	{
		float progress;

		memcpy(&progress, &value->value, sizeof(progress));
		host_load_trace("loading.progress", "progress=%.3f", (double)progress);
		CALL(loading_progress, progress);
		break;
	}
	case XGPU_STREAM_LOADING_OWNED:
	{
		uint32_t owned = host_render_stream_loading_holds() && backend->loading_owned &&
			backend->loading_owned(context);

		answer(((const struct xgpu_stream_reply_record *)start)->reply, &owned, sizeof(owned));
		break;
	}
	case XGPU_STREAM_LOADING:
		if (value->value > 2)
			return XGPU_STREAM_ERROR_RECORD;
		loading_set(backend, (int)((const struct xgpu_stream_value *)start)->value);
		break;
	case XGPU_STREAM_SKIP:
		break;
	case XGPU_STREAM_INITIALIZE:
	{
		struct xgpu_stream_capabilities capabilities;

		memset(&capabilities, 0, sizeof(capabilities));
		CALL(initialize, &capabilities);
		answer(((const struct xgpu_stream_reply_record *)start)->reply, &capabilities, sizeof(capabilities));
		break;
	}
	case XGPU_STREAM_INVALIDATE:
		CALL0(invalidate);
		break;
	case XGPU_STREAM_FLUSH:
		CALL0(flush);
		break;
	case XGPU_STREAM_ANTI_ALIASING_PREPARE:
		CALL(anti_aliasing_prepare, value->value);
		break;
	case XGPU_STREAM_ANTI_ALIAS:
		CALL(anti_alias, (const void *)start);
		break;
	case XGPU_STREAM_HDR:
		CALL(hdr, (const void *)start);
		break;
	case XGPU_STREAM_TONE_MAP:
		CALL(tone_map, (const void *)start);
		break;
	case XGPU_STREAM_AMBIENT_OCCLUSION:
		CALL(ambient_occlusion, (const void *)start);
		break;
	case XGPU_STREAM_PICTURE_POSITION:
		CALL(picture_top, value->value);
		break;
	case XGPU_STREAM_GAMMA:
		CALL(gamma, value->value);
		break;
	case XGPU_STREAM_TARGET_CREATE:
		CALL(target_create, (const void *)start);
		break;
	case XGPU_STREAM_TARGET_DELETE:
		CALL(target_delete, value->value);
		break;
	case XGPU_STREAM_TARGET_MULTISAMPLE:
		CALL(target_multisample, (const void *)start);
		break;
	case XGPU_STREAM_TARGET_RESOLVE:
		CALL(target_resolve, value->value);
		break;
	case XGPU_STREAM_SET_TARGETS:
		CALL(set_targets, (const void *)start);
		break;
	case XGPU_STREAM_CLEAR:
	{
		const struct xgpu_stream_clear *clear = (const void *)start;

		if (clear->count > (size - sizeof(*clear)) / sizeof(clear->rectangles[0]))
			return XGPU_STREAM_ERROR_RECORD;
		CALL(clear, clear);
		break;
	}
	case XGPU_STREAM_CLEAR_TARGET:
		CALL(clear_target, (const void *)start);
		break;
	case XGPU_STREAM_READ_TARGET:
	{
		const struct xgpu_stream_read_target *read = (const void *)start;
		uint32_t written = 0;

		if (backend->read_target && read->pixels && read->size)
			written = backend->read_target(context, read) ? 1 : 0;
		answer(read->reply, &written, sizeof(written));
		break;
	}
	case XGPU_STREAM_SCREENSHOT:
	{
		const struct xgpu_stream_screenshot *shot = (const void *)start;
		uint32_t written = screenshot(backend, context, shot, size);

		answer(shot->reply, &written, sizeof(written));
		break;
	}
	case XGPU_STREAM_SET_TEXTURES:
	{
		const struct xgpu_stream_set_textures *textures = (const void *)start;
		const unsigned char *place = (const unsigned char *)(textures + 1);
		const unsigned char *end = start + size;
		int stage;

		for (stage = 0; stage < 4; stage++)
		{
			struct host_render_texture_stage *to = &decoder->stages[stage];
			uint32_t level_bytes;

			if (!(textures->changed & (1u << stage)))
				continue;
			if ((size_t)(end - place) < sizeof(to->stage))
				return XGPU_STREAM_ERROR_RECORD;
			memcpy(&to->stage, place, sizeof(to->stage));
			place += sizeof(to->stage);
			if (to->stage.level_count > XGPU_STREAM_MIP_LEVELS)
				return XGPU_STREAM_ERROR_RECORD;
			level_bytes = to->stage.level_count * sizeof(to->levels[0]);
			if ((size_t)(end - place) < level_bytes)
				return XGPU_STREAM_ERROR_RECORD;
			memset(to->levels, 0, sizeof(to->levels));
			memcpy(to->levels, place, level_bytes);
			place += level_bytes;
		}
		CALL(set_textures, decoder->stages);
		break;
	}
	case XGPU_STREAM_CONSTANTS:
	{
		const struct xgpu_stream_constants *constants = (const void *)start;

		if (constants->count > (size - sizeof(*constants)) / sizeof(constants->values[0]) ||
			constants->first > 192 || constants->count > 192 - constants->first)
			return XGPU_STREAM_ERROR_RECORD;
		CALL(set_constants, constants);
		break;
	}
	case XGPU_STREAM_VERTEX_PROGRAM:
	{
		const struct xgpu_stream_vertex_program *program = (const void *)start;

		if (program->instruction_count > XGPU_STREAM_VERTEX_PROGRAM_MAXIMUM ||
			program->instruction_count > (size - sizeof(*program)) / XGPU_STREAM_VERTEX_INSTRUCTION_SIZE)
			return XGPU_STREAM_ERROR_RECORD;
		CALL(vertex_program, program);
		break;
	}
	case XGPU_STREAM_VERTEX_LIGHTING:
		CALL(vertex_lighting, (const void *)start);
		break;
	case XGPU_STREAM_PIPELINE_DEFINE:
	{
		const struct xgpu_stream_pipeline_define *define = (const void *)start;
		struct xgpu_stream_pipeline_define **slot;

		if (!define->id || define->id >= XGPU_STREAM_PIPELINES)
			return XGPU_STREAM_ERROR_ID;
		slot = &decoder->pipelines[define->id];
		if (!*slot && !(*slot = malloc(sizeof(**slot))))
			return XGPU_STREAM_ERROR_ID;
		memcpy(*slot, define, sizeof(**slot));
		__atomic_store_n(&decoder->pipeline_states[define->id], XGPU_STREAM_PIPELINE_UNKNOWN, __ATOMIC_RELAXED);
		CALL(pipeline_define, *slot);
		break;
	}
	case XGPU_STREAM_PIPELINE:
	{
		uint32_t id = value->value;
		int made = 1;

		if (!id || id >= XGPU_STREAM_PIPELINES || !decoder->pipelines[id])
			return XGPU_STREAM_ERROR_ID;
		if (backend->pipeline)
			made = backend->pipeline(context, id, decoder->pipelines[id]);
		if (__atomic_load_n(&decoder->pipeline_states[id], __ATOMIC_RELAXED) == XGPU_STREAM_PIPELINE_UNKNOWN)
			__atomic_store_n(&decoder->pipeline_states[id], made ? XGPU_STREAM_PIPELINE_MADE : XGPU_STREAM_PIPELINE_FAILED,
				__ATOMIC_RELAXED);
		break;
	}
	case XGPU_STREAM_DRAW_STATE:
	{
		const struct xgpu_stream_draw_state *state = (const void *)start;
		const unsigned char *place = (const unsigned char *)(state + 1);
		unsigned char *uniforms = (unsigned char *)&decoder->uniforms;
		uint32_t pieces = (uint32_t)__builtin_popcount(state->pieces[0]) + (uint32_t)__builtin_popcount(state->pieces[1]);
		uint32_t needed = sizeof(*state) + (state->parts & XGPU_STREAM_DRAW_DYNAMIC ? sizeof(decoder->dynamic) : 0) +
			pieces * 16;
		uint32_t piece;

		if (needed > size || (XGPU_STREAM_UNIFORM_PIECES < 64 && state->pieces[1] >> (XGPU_STREAM_UNIFORM_PIECES - 32)))
			return XGPU_STREAM_ERROR_RECORD;
		if (state->parts & XGPU_STREAM_DRAW_DYNAMIC)
		{
			memcpy(&decoder->dynamic, place, sizeof(decoder->dynamic));
			place += sizeof(decoder->dynamic);
		}
		for (piece = 0; piece < XGPU_STREAM_UNIFORM_PIECES; piece++)
		{
			uint32_t offset = piece * 16;

			if (!(state->pieces[piece / 32] & (1u << (piece % 32))))
				continue;
			memcpy(uniforms + offset, place, sizeof(decoder->uniforms) - offset < 16 ? sizeof(decoder->uniforms) - offset : 16);
			place += 16;
		}
		if (state->parts & XGPU_STREAM_DRAW_UNIFORMS)
			decoder->uniforms_serial = state->uniforms_serial;
		CALL(draw_state, &decoder->dynamic, &decoder->uniforms, decoder->uniforms_serial);
		break;
	}
	case XGPU_STREAM_MIRROR_WRITE:
	{
		const struct xgpu_stream_mirror_write *write = (const void *)start;

		if (!data_inside(decoder, start, size, write->data, write->size))
			return XGPU_STREAM_ERROR_BOUNDS;
		CALL(mirror_write, write);
		break;
	}
	case XGPU_STREAM_LAYOUT_DEFINE:
	{
		const struct xgpu_stream_layout_define *define = (const void *)start;
		struct xgpu_stream_layout_define **slot;

		if (!define->id || define->id >= XGPU_STREAM_LAYOUTS)
			return XGPU_STREAM_ERROR_ID;
		if (define->element_count > 16 ||
			define->element_count > (size - sizeof(*define)) / sizeof(define->elements[0]))
			return XGPU_STREAM_ERROR_RECORD;
		slot = &decoder->layouts[define->id];
		if (!*slot && !(*slot = malloc(sizeof(**slot) + 16 * sizeof(define->elements[0]))))
			return XGPU_STREAM_ERROR_ID;
		memcpy(*slot, define, sizeof(*define) + define->element_count * sizeof(define->elements[0]));
		break;
	}
	case XGPU_STREAM_VERTICES:
	{
		const struct xgpu_stream_vertices *vertices = (const void *)start;
		const struct xgpu_stream_vertex_stream *streams = (const void *)(vertices + 1);
		struct host_render_vertex_input *input = &decoder->input;
		uint32_t stream_count = (uint32_t)__builtin_popcount(vertices->streams);
		uint32_t attribute_count = (uint32_t)__builtin_popcount(vertices->attributes);
		const float (*values)[4];
		int s, reg;

		if (!vertices->layout || vertices->layout >= XGPU_STREAM_LAYOUTS || !decoder->layouts[vertices->layout])
			return XGPU_STREAM_ERROR_ID;
		if (sizeof(*vertices) + stream_count * sizeof(*streams) + attribute_count * 16 > size)
			return XGPU_STREAM_ERROR_RECORD;
		input->layout = decoder->layouts[vertices->layout];
		input->present = vertices->streams;
		memset(input->streams, 0, sizeof(input->streams));
		for (s = 0; s < 16; s++)
		{
			if (!(vertices->streams & (1u << s)))
				continue;
			if (streams->segment < 0 && !data_inside(decoder, start, size, streams->data, streams->bytes))
				return XGPU_STREAM_ERROR_BOUNDS;
			input->streams[s] = *streams++;
		}
		values = (const void *)streams;
		for (reg = 0; reg < 16; reg++)
		{
			if (vertices->attributes & (1u << reg))
				memcpy(input->attributes[reg], *values++, 16);
		}
		CALL(set_vertices, input);
		break;
	}
	case XGPU_STREAM_DRAW:
		CALL(draw, (const void *)start);
		break;
	case XGPU_STREAM_DRAW_INDICES:
	{
		const struct xgpu_stream_draw_indices *draw = (const void *)start;

		if (draw->count > 0x7fffffffu || !data_inside(decoder, start, size, draw->indices, draw->count * 2))
			return XGPU_STREAM_ERROR_BOUNDS;
		CALL(draw_indices, draw);
		break;
	}
	case XGPU_STREAM_DRAW_INDEXED:
	{
		const struct xgpu_stream_draw_indexed *draw = (const void *)start;

		if (draw->segment < 0 &&
			(draw->count > 0x7fffffffu || !data_inside(decoder, start, size, draw->indices, draw->count * 2)))
			return XGPU_STREAM_ERROR_BOUNDS;
		CALL(draw_indexed, draw);
		break;
	}
	case XGPU_STREAM_DRAW_IMMEDIATE:
	{
		const struct xgpu_stream_draw_immediate *draw = (const void *)start;
		const float (*values)[4] = (const void *)(draw + 1);
		uint64_t vertex_bytes = (uint64_t)draw->count * (uint32_t)__builtin_popcount(draw->written) * 16;
		int reg;

		if (sizeof(*draw) + (uint32_t)__builtin_popcount(draw->attributes) * 16 > size)
			return XGPU_STREAM_ERROR_RECORD;
		if (vertex_bytes > 0xffffffffu || !data_inside(decoder, start, size, draw->vertices, (uint32_t)vertex_bytes) ||
			draw->index_count > 0x7fffffffu || !data_inside(decoder, start, size, draw->indices, draw->index_count * 2))
			return XGPU_STREAM_ERROR_BOUNDS;
		for (reg = 0; reg < 16; reg++)
		{
			if (draw->attributes & (1u << reg))
				memcpy(decoder->immediate_attributes[reg], *values++, 16);
		}
		CALL(draw_immediate, draw, (const float (*)[4])decoder->immediate_attributes);
		break;
	}
	case XGPU_STREAM_VISIBILITY_BEGIN:
		CALL0(visibility_begin);
		break;
	case XGPU_STREAM_VISIBILITY_END:
		if (((const struct xgpu_stream_visibility_end *)start)->slot >= XGPU_STREAM_VISIBILITY_SLOTS)
			return XGPU_STREAM_ERROR_ID;
		CALL(visibility_end, (const void *)start);
		break;
	case XGPU_STREAM_VISIBILITY_FRAME:
		CALL0(visibility_frame);
		break;
	case XGPU_STREAM_PRESENT:
		CALL(present, value->value);
		break;
	case XGPU_STREAM_FRAME_END:
		decoder->frame_input = 0;
		if (size >= sizeof(struct xgpu_stream_frame_end))
		{
			const struct xgpu_stream_frame_end *end = (const void *)start;

			decoder->frame_input = (uint64_t)end->input_high << 32 | end->input_low;
		}
		CALL0(frame_end);
		decoder->frame_ended = 1;
		break;
	case XGPU_STREAM_DYNAMIC_LIGHTS_PREPARE:
		if (backend->dynamic_lights_prepare)
			backend->dynamic_lights_prepare(context);
		break;
	case XGPU_STREAM_DYNAMIC_LIGHTS_UPLOAD:
	{
		const struct xgpu_stream_dynamic_lights_upload *upload = (const void *)start;

		if (upload->count > XGPU_STREAM_DYNAMIC_LIGHTS_MAXIMUM ||
			!data_inside(decoder, start, size, upload->cells, upload->size))
			return XGPU_STREAM_ERROR_BOUNDS;
		CALL(dynamic_lights_upload, upload);
		break;
	}
	case XGPU_STREAM_DYNAMIC_LIGHTS_HALOS:
	{
		const struct xgpu_stream_dynamic_lights_halos *halos = (const void *)start;

		if (halos->count < 0 || halos->count > XGPU_STREAM_DYNAMIC_LIGHTS_MAXIMUM ||
			(uint32_t)halos->count * 32 > size - sizeof(*halos))
			return XGPU_STREAM_ERROR_RECORD;
		CALL(dynamic_lights_halos, halos);
		break;
	}
	case XGPU_STREAM_PREPARE:
		host_load_trace("prepare.begin", "stream");
		CALL0(prepare);
		break;
	case XGPU_STREAM_PREPARED:
	{
		uint32_t milliseconds;

		host_load_trace("prepare.wait.begin", "stream");
		milliseconds = backend->prepared ? backend->prepared(context) : 0;
		host_load_trace("prepare.end", "wait_ms=%u", milliseconds);

		answer(((const struct xgpu_stream_reply_record *)start)->reply, &milliseconds, sizeof(milliseconds));
		break;
	}
	case XGPU_STREAM_VR_SUPPORTED:
	{
		uint32_t supported = backend->vr_supported ? (backend->vr_supported(context) ? 1 : 0) : 0;

		answer(((const struct xgpu_stream_reply_record *)start)->reply, &supported, sizeof(supported));
		break;
	}
	case XGPU_STREAM_VR_INITIALIZE:
	{
		uint32_t reply = ((const struct xgpu_stream_reply_record *)start)->reply;
		struct xgpu_stream_vr_info info;

		if (!reply)
			break;
		memcpy(&info, host_pointer(reply), sizeof(info));
		CALL(vr_initialize, &info);
		answer(reply, &info, sizeof(info));
		break;
	}
	case XGPU_STREAM_VR_EYES:
		CALL(vr_eyes, (const void *)start);
		break;
	case XGPU_STREAM_VR_PASS:
		CALL(vr_pass, (int32_t)value->value);
		break;
	case XGPU_STREAM_VR_GROUPS_RESET:
		CALL0(vr_groups_reset);
		break;
	case XGPU_STREAM_VR_TIMING:
		CALL(vr_timing, (int32_t)value->value);
		break;
	case XGPU_STREAM_VR_COMPOSE:
		CALL(vr_compose, &((const struct xgpu_stream_vr_frame_record *)start)->frame);
		break;
	case XGPU_STREAM_VR_SUBMIT:
		CALL(vr_submit, (const void *)start);
		break;
	case XGPU_STREAM_VR_STATE:
		CALL(vr_state, (const void *)start);
		break;
	case XGPU_STREAM_VR_EYE_TRANSFORMS:
		CALL(vr_eye_transforms, (const void *)start);
		break;
	case XGPU_STREAM_VR_SCREEN_CORRECTIONS:
		CALL(vr_screen_corrections, (const void *)start);
		break;
	case XGPU_STREAM_VR_SKY:
		CALL(vr_sky, (int32_t)value->value);
		break;
	case XGPU_STREAM_VR_EYE_FEED:
		CALL(vr_eye_feed, ((const struct xgpu_stream_vr_values *)start)->values);
		break;
	case XGPU_STREAM_VR_EYE_TINT:
		CALL(vr_eye_tint, ((const struct xgpu_stream_vr_values *)start)->values);
		break;
	default:
		return XGPU_STREAM_ERROR_RECORD;
	}
	return XGPU_STREAM_ERROR_NONE;
}

uint32_t host_render_decode(struct host_render_decoder *decoder, uint32_t from, uint32_t to)
{
	struct xgpu_stream_ring *ring = decoder->ring;
	uint32_t position = from;

	decoder->frame_ended = 0;
	while (position != to && !decoder->frame_ended)
	{
		uint32_t offset = position & decoder->mask;
		uint32_t available = to - position;
		const unsigned char *start = decoder->buffer + offset;
		uint32_t header, type, size;
		int error;
		double trace_start = -1.0;

		header = *(const uint32_t *)start;
		type = XGPU_STREAM_HEADER_TYPE(header);
		size = XGPU_STREAM_HEADER_SIZE(header);
		if (type == XGPU_STREAM_PREPARE)
		{
			decoder->trace_preparing = host_load_trace_enabled();
			memset(decoder->trace_operations, 0, sizeof(decoder->trace_operations));
		}
		if (decoder->trace_preparing)
			trace_start = host_load_trace_clock(CLOCK_MONOTONIC);
		if (type == XGPU_STREAM_SKIP && !size)
			size = decoder->size - offset;
		if (size > available || size > decoder->size - offset)
		{
			error = XGPU_STREAM_ERROR_BOUNDS;
		}
		else if (type >= XGPU_STREAM_RECORD_TYPES || size < record_minimum[type] || size % XGPU_STREAM_RECORD_ALIGNMENT)
		{
			error = XGPU_STREAM_ERROR_RECORD;
		}
		else if (host_render_record_active)
		{
			/* (recorded, or the replay's memory restored: host_render_record.h) */
			host_render_record_before(ring, position, start, type, size);
			error = decode_record(decoder, start, type, size);
			host_render_record_after();
		}
		else
		{
			error = decode_record(decoder, start, type, size);
		}
		if (trace_start >= 0.0 && type < XGPU_STREAM_RECORD_TYPES)
		{
			double elapsed = host_load_trace_clock(CLOCK_MONOTONIC) - trace_start;

			if (elapsed >= 0.0)
			{
				decoder->trace_operations[type].count++;
				decoder->trace_operations[type].seconds += elapsed;
				if (elapsed > decoder->trace_operations[type].maximum)
					decoder->trace_operations[type].maximum = elapsed;
			}
		}
		if (type == XGPU_STREAM_PREPARED && decoder->trace_preparing)
		{
			uint32_t operation;

			decoder->trace_preparing = 0;
			for (operation = 0; operation < XGPU_STREAM_RECORD_TYPES; operation++)
			{
				if (decoder->trace_operations[operation].count)
					host_load_trace("prepare.operation", "opcode=%u count=%llu seconds=%.6f maximum=%.6f", operation,
						(unsigned long long)decoder->trace_operations[operation].count,
						decoder->trace_operations[operation].seconds, decoder->trace_operations[operation].maximum);
			}
		}
		if (error)
		{
			ring->error = (uint32_t)error;
			ring->error_position = position;
			host_logf(HOST_LOG_ERROR, "render stream: record %u of %u bytes at %u: error %d", type, size, position,
				error);
			return position;
		}
		position += size;
		__atomic_store_n(&ring->decoded, position, __ATOMIC_RELEASE);
		host_render_stream_loading_service();
	}
	return position;
}

/* ---------- the render thread */

#define FENCE_QUEUE 64

struct fence_entry
{
	/* the ring's bytes it covers, to there; the frames it ends */
	uint32_t position;
	uint32_t frames;
	uint64_t fence;
};

static const struct host_render_backend *render_backend;

static struct
{
	pthread_mutex_t lock;
	pthread_cond_t work;    /* for the render thread: records, a waiting guest, the end */
	pthread_cond_t done;    /* for the guest: bytes released, frames retired */
	pthread_t thread;
	int running, stopping;
	struct xgpu_stream_ring *ring;
	struct host_render_decoder *decoder;
	uint32_t committed, decoded;
	/* positions the guest waits to see released (room, or a synchronous
	record), while it does */
	int guest_waiting;
	struct fence_entry fences[FENCE_QUEUE];
	uint32_t fence_first, fence_count;
	/* the position the last fence covers, the frames played and not yet
	in a fence */
	uint32_t fenced;
	uint32_t frames_unfenced;
	uint32_t retired_frames;
	uint64_t frame_started;
	/* (frame_pacing.h) the backend paces the game by its display; the
	frames the display has taken (shown, or dropped); its last latch and its
	refreshes' period (nanoseconds of the game's clock); the frames' costs,
	their input read to ready for the display */
	int display_paced;
	uint32_t frames_taken;
	uint64_t latch, period;
	struct frame_pacing_costs costs;
	/* the refresh each of the last frames was started for (by its number,
	as frames_submitted counts it), and whether it waited for its start;
	the frames played when the stream opened, and frames_submitted then */
	struct
	{
		uint32_t frame;
		int waited;
		uint64_t target, margin;
	} targets[8];
	uint32_t played_at_open, submitted_at_open;
	/* the margin fixed by HALO_FRAME_PACING_MARGIN_MS (milliseconds; for
	tests of a display's lead: not learned then) */
	int margin_fixed;
	struct host_render_pacing_statistics pacing_statistics;
	/* (the render thread's) when the frame played was ready, if the
	backend said (host_render_stream_frame_ready); when each of the last
	frames was submitted (by the frames submitted, and played, since the
	stream opened), for the time it waited for the render thread */
	uint64_t frame_ready;
	uint64_t frame_submitted[8];
	uint32_t frames_committed, frames_played;
	/* a function to run on the render thread once the records before it
	are played (host_render_stream_call), and the calls run so far */
	void (*call)(void *argument);
	void *call_argument;
	uint32_t call_position;
	uint64_t calls_run;
} render =
{
	.lock = PTHREAD_MUTEX_INITIALIZER,
	.work = PTHREAD_COND_INITIALIZER,
	.done = PTHREAD_COND_INITIALIZER,
};

static uint64_t monotonic_nanoseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

void host_render_stream_set_backend(const struct host_render_backend *backend)
{
	render_backend = backend;
}

void host_render_stream_gpu_completed(void)
{
	pthread_mutex_lock(&render.lock);
	pthread_cond_signal(&render.work);
	pthread_mutex_unlock(&render.lock);
}

/* (render.lock held) a fence over what has been played, unless there is
nothing new */
static void fence_push(void)
{
	const struct host_render_backend *backend = render_backend;
	struct fence_entry *entry;
	uint64_t fence;

	if (render.fenced == render.decoded && !render.frames_unfenced)
		return;
	if (render.fence_count == FENCE_QUEUE)
		return;
	pthread_mutex_unlock(&render.lock);
	fence = backend->fence ? backend->fence(backend->context) : 0;
	pthread_mutex_lock(&render.lock);
	entry = &render.fences[(render.fence_first + render.fence_count) % FENCE_QUEUE];
	entry->position = render.decoded;
	entry->frames = render.frames_unfenced;
	entry->fence = fence;
	render.fence_count++;
	render.fenced = render.decoded;
	render.frames_unfenced = 0;
}

/* (render.lock held) waits for the oldest fence; its bytes released, its
frames retired */
static void fence_retire(void)
{
	const struct host_render_backend *backend = render_backend;
	struct fence_entry entry = render.fences[render.fence_first];

	pthread_mutex_unlock(&render.lock);
	if (backend->wait)
		backend->wait(backend->context, entry.fence);
	pthread_mutex_lock(&render.lock);
	render.fence_first = (render.fence_first + 1) % FENCE_QUEUE;
	render.fence_count--;
	render.retired_frames += entry.frames;
	__atomic_store_n(&render.ring->frames_retired, render.retired_frames, __ATOMIC_RELEASE);
	__atomic_store_n(&render.ring->released, entry.position, __ATOMIC_RELEASE);
	pthread_cond_broadcast(&render.done);
}

/* after a malformed record: nothing more is played, and nothing waits */
static void stream_failed(void)
{
	render.decoded = render.fenced = render.committed;
	render.fence_count = 0;
	render.retired_frames = render.ring->frames_submitted;
	__atomic_store_n(&render.ring->frames_retired, render.retired_frames, __ATOMIC_RELEASE);
	__atomic_store_n(&render.ring->released, render.committed, __ATOMIC_RELEASE);
	pthread_cond_broadcast(&render.done);
}

static _Atomic int render_playing;

#ifdef __APPLE__
/* The render thread's autorelease pool (Apple's), drained after each run of
records played. A thread of its own has none: what Metal's backend
autoreleases (its encoders, buffers, the descriptors and keys it makes) was
kept until the thread ended, some 9,000 objects a second on an M1 Max in
a busy game. The runtime's functions are found as the thread starts (none
where the Objective-C runtime is not loaded: the stream's tests). */
static struct
{
	void *(*push)(void);
	void (*pop)(void *pool);
} autorelease;

static void *autorelease_push(void)
{
	return autorelease.push ? autorelease.push() : NULL;
}

static void autorelease_pop(void *pool)
{
	if (pool)
		autorelease.pop(pool);
}
#endif

int host_render_stream_playing(void)
{
	return atomic_load(&render_playing);
}

static void *render_thread(void *argument)
{
	const struct host_render_backend *backend = render_backend;
#ifdef __APPLE__
	void *pool;

	autorelease.push = (void *(*)(void))dlsym(RTLD_DEFAULT, "objc_autoreleasePoolPush");
	autorelease.pop = (void (*)(void *))dlsym(RTLD_DEFAULT, "objc_autoreleasePoolPop");
	if (!autorelease.pop)
		autorelease.push = NULL;
	pool = autorelease_push();
#endif

	(void)argument;
	pthread_mutex_lock(&render.lock);
	for (;;)
	{
		if (loading.frame.active)
		{
			pthread_mutex_unlock(&render.lock);
			host_render_stream_loading_service();
			pthread_mutex_lock(&render.lock);
		}
		if (render.ring->error)
		{
			stream_failed();
			if (render.stopping)
				break;
			pthread_cond_wait(&render.work, &render.lock);
			continue;
		}
		if (render.call && (int32_t)(render.decoded - render.call_position) >= 0)
		{
			void (*function)(void *) = render.call;
			void *argument = render.call_argument;

			pthread_mutex_unlock(&render.lock);
			function(argument);
			pthread_mutex_lock(&render.lock);
			render.call = NULL;
			render.calls_run++;
			pthread_cond_broadcast(&render.done);
			continue;
		}
		/* Release completed work before playing more. While the GPU is busy,
		keep accepting records instead of blocking in the oldest fence's wait.
		The completion notification takes render.lock, so it cannot be lost
		between this query and the condition wait below. */
		if (render.fence_count && backend->fence_ready &&
			backend->fence_ready(backend->context, render.fences[render.fence_first].fence))
		{
			fence_retire();
			continue;
		}
		if (render.decoded != render.committed)
		{
			uint32_t from = render.decoded, to = render.committed, reached;
			int frame_ended;

			pthread_mutex_unlock(&render.lock);
			if (!render.frame_started)
				render.frame_started = monotonic_nanoseconds();
			atomic_store(&render_playing, 1);
			reached = host_render_decode(render.decoder, from, to);
			atomic_store(&render_playing, 0);
#ifdef __APPLE__
			/* (what the records, and the fences and calls since the last,
			autoreleased) */
			autorelease_pop(pool);
			pool = autorelease_push();
#endif
			frame_ended = render.decoder->frame_ended;
			if (frame_ended)
			{
				uint64_t ready = render.frame_ready ? render.frame_ready : monotonic_nanoseconds();
				uint64_t submitted, input = render.decoder->frame_input;

				/* (a frame's cost is its own work: the game's, and the render
				thread's once it is free; not its wait for the thread, playing
				or showing the frame before, which the frame's start in time
				avoids) */
				pthread_mutex_lock(&render.lock);
				submitted = render.frame_submitted[++render.frames_played % 8];
				if (input && submitted > input && render.frame_started > submitted)
					input += render.frame_started - submitted;
				frame_pacing_cost_add(&render.costs, input, ready);
				pthread_mutex_unlock(&render.lock);
				render.frame_ready = 0;
				render.ring->decode_nanoseconds = (uint32_t)(monotonic_nanoseconds() - render.frame_started);
				render.frame_started = 0;
			}
			pthread_mutex_lock(&render.lock);
			if (render.ring->error)
				continue;
			render.decoded = reached;
			if (frame_ended)
			{
				render.frames_unfenced++;
				fence_push();
			}
			continue;
		}
		else if (render.guest_waiting && render.fenced != render.decoded && render.fence_count < FENCE_QUEUE)
		{
			/* the guest waits on what is played and not yet fenced */
			fence_push();
			continue;
		}
		else if (render.fence_count && (!backend->fence_ready || render.stopping))
		{
			/* Drain at shutdown; backends without completion notifications
			still wait here when there is nothing to play. */
			fence_retire();
			continue;
		}
		if (render.stopping)
			break;
		if (loading.frame.active)
		{
			struct timespec until;

			clock_gettime(CLOCK_REALTIME, &until);
			until.tv_nsec += 1000000;
			if (until.tv_nsec >= 1000000000)
			{
				until.tv_sec++;
				until.tv_nsec -= 1000000000;
			}
			pthread_cond_timedwait(&render.work, &render.lock, &until);
		}
		else
			pthread_cond_wait(&render.work, &render.lock);
	}
	pthread_mutex_unlock(&render.lock);
	if (loading.frame.active)
		loading_set(loading.backend, 0);
#ifdef __APPLE__
	autorelease_pop(pool);
#endif
	return NULL;
}

/* the render thread; on macOS, at the QoS of an app's user interface: each
frame is the display's (pthread_attr_set_qos_class_np) */
static int render_thread_start(void)
{
	pthread_attr_t attributes;
	int result;

	pthread_attr_init(&attributes);
#ifdef __APPLE__
	pthread_attr_set_qos_class_np(&attributes, QOS_CLASS_USER_INTERACTIVE, 0);
#endif
	result = pthread_create(&render.thread, &attributes, render_thread, NULL);
	pthread_attr_destroy(&attributes);
	return result;
}

int host_render_stream_open(uint32_t ring_address)
{
	const struct host_render_backend *backend = render_backend;
	struct xgpu_stream_ring *ring = host_pointer(ring_address);

	if (!backend || !ring || render.running)
		return -1;
	if (ring->magic != XGPU_STREAM_MAGIC || ring->version != XGPU_STREAM_VERSION ||
		ring->header_size != sizeof(*ring))
	{
		host_logf(HOST_LOG_ERROR, "render stream: the guest's ring is of another version (%u)", ring->version);
		return -1;
	}
	if (ring->size < 0x10000 || (ring->size & (ring->size - 1)) || ring->buffer % XGPU_STREAM_RING_ALIGNMENT ||
		ring->buffer + ring->size < ring->buffer || !ring->frames_in_flight || ring->frames_in_flight > 8 ||
		!ring->visibility || !ring->pipelines)
	{
		host_logf(HOST_LOG_ERROR, "render stream: the guest's ring is out of place");
		return -1;
	}
	render.ring = ring;
	host_render_stream_display_hdr(-1, 0.0f, 0.0f, 0);
	render.decoder = host_render_decoder_new(backend, ring);
	if (!render.decoder)
		return -1;
	render.committed = render.decoded = render.fenced = ring->committed;
	render.fence_first = render.fence_count = 0;
	render.frames_unfenced = 0;
	render.retired_frames = ring->frames_retired = ring->frames_submitted;
	ring->released = ring->committed;
	render.frames_taken = ring->frames_submitted;
	render.played_at_open = render.frames_played;
	render.submitted_at_open = ring->frames_submitted;
	if (getenv("HALO_FRAME_PACING_MARGIN_MS") && atof(getenv("HALO_FRAME_PACING_MARGIN_MS")) > 0.0)
	{
		render.costs.margin = (uint64_t)(atof(getenv("HALO_FRAME_PACING_MARGIN_MS")) * 1e6);
		render.margin_fixed = 1;
	}
	ring->error = 0;
	render.stopping = 0;
	if (backend->open)
		backend->open(backend->context, host_pointer(ring->buffer), ring->size, host_pointer(ring->visibility));
	host_render_record_start(ring);
	if (render_thread_start())
	{
		host_render_decoder_free(render.decoder);
		render.decoder = NULL;
		return -1;
	}
	render.running = 1;
	return 0;
}

/* (render.lock held) the guest waits for done, counted in its statistics */
/* ---------- display.hdr's display */

/* what the backend last said of its display (headroom, white: a float's
bits) */
static _Atomic uint32_t display_hdr, display_headroom = 0x3f800000u, display_white, display_white_reported;

void host_render_stream_display_hdr(int hdr, float headroom, float white_nits, int white_reported)
{
	struct xgpu_stream_ring *ring = render.ring;

	/* (-1: as last said, into a ring just opened) */
	if (hdr >= 0)
	{
		uint32_t bits, white_bits;

		if (!(headroom >= 1.0f))
			headroom = 1.0f;
		if (!(white_nits > 0.0f))
			white_nits = 0.0f;
		memcpy(&bits, &headroom, sizeof(bits));
		memcpy(&white_bits, &white_nits, sizeof(white_bits));
		atomic_store(&display_white, white_bits);
		atomic_store(&display_white_reported, white_reported ? 1u : 0u);
		atomic_store(&display_hdr, hdr ? 1u : 0u);
		atomic_store(&display_headroom, bits);
	}
	if (ring)
	{
		__atomic_store_n(&ring->display_white, atomic_load(&display_white), __ATOMIC_RELEASE);
		__atomic_store_n(&ring->display_white_reported, atomic_load(&display_white_reported), __ATOMIC_RELEASE);
		__atomic_store_n(&ring->display_headroom, atomic_load(&display_headroom), __ATOMIC_RELEASE);
		__atomic_store_n(&ring->display_hdr, atomic_load(&display_hdr), __ATOMIC_RELEASE);
	}
}

/* ---------- pacing by the display (frame_pacing.h) */

static struct frame_pacing_latency latency;

void host_render_stream_display_paced(int paced)
{
	pthread_mutex_lock(&render.lock);
	render.display_paced = paced;
	pthread_cond_broadcast(&render.done);
	pthread_mutex_unlock(&render.lock);
}

uint64_t host_render_stream_frame_input(void)
{
	return render.decoder ? render.decoder->frame_input : 0;
}

void host_render_stream_frame_taken(void)
{
	pthread_mutex_lock(&render.lock);
	render.frames_taken++;
	pthread_cond_broadcast(&render.done);
	pthread_mutex_unlock(&render.lock);
}

void host_render_stream_display_latched(uint64_t latch, uint64_t period)
{
	pthread_mutex_lock(&render.lock);
	render.latch = latch;
	render.period = period;
	pthread_mutex_unlock(&render.lock);
}

void host_render_stream_frames_taken_reset(void)
{
	pthread_mutex_lock(&render.lock);
	render.frames_taken = render.submitted_at_open + (render.frames_played - render.played_at_open);
	pthread_cond_broadcast(&render.done);
	pthread_mutex_unlock(&render.lock);
}

void host_render_stream_frame_displayed(uint64_t input, uint64_t finished, uint64_t latched, uint64_t shown,
	uint64_t period)
{
	uint32_t frame;

	pthread_mutex_lock(&render.lock);
	/* (never more than the frames played, and the one playing: after
	display_wait's own count, of a display that took nothing for
	FRAME_PACING_WAIT_MAXIMUM_NS) */
	frame = render.submitted_at_open + (render.frames_played - render.played_at_open) + 1;
	if ((int32_t)(frame - render.frames_taken) <= 0)
		render.frames_taken = frame - 1;
	frame = ++render.frames_taken;
	if (!latched)
		latched = shown;
	if (latched)
	{
		/* (taken a refresh or more after the one it was started for: a
		miss. The margin is widened for a frame that started at its time and
		whose work was done by the margin before that refresh (or not
		known): the display wants its frames sooner than the margin gives.
		A frame whose work ran past it took longer than any of the last
		ones, which its cost, now counted, gives the frames after it) */
		if (render.targets[frame % 8].frame == frame && render.targets[frame % 8].target && period &&
			latched < render.targets[frame % 8].target + 4 * period)
		{
			if (latched > render.targets[frame % 8].target + period / 2)
			{
				render.pacing_statistics.missed++;
				if (render.targets[frame % 8].waited && (!finished || finished + render.targets[frame % 8].margin <=
					render.targets[frame % 8].target + FRAME_PACING_MARGIN_STEP_NS / 4))
				{
					/* (once for the margin it started with: the frames started
					before a miss widened it, already on their way, miss too) */
					if (!render.margin_fixed && render.targets[frame % 8].margin >=
						(render.costs.margin ? render.costs.margin : FRAME_PACING_MARGIN_NS))
						frame_pacing_margin_missed(&render.costs, period);
				}
				else
				{
					render.pacing_statistics.overran++;
				}
			}
			else
			{
				render.pacing_statistics.kept++;
				if (!render.margin_fixed)
					frame_pacing_margin_kept(&render.costs);
			}
		}
		render.latch = latched;
		if (period)
			render.period = period;
	}
	pthread_cond_broadcast(&render.done);
	pthread_mutex_unlock(&render.lock);
	if (shown)
		frame_pacing_latency_add(&latency, input, shown);
}

void host_render_stream_frame_finished(uint64_t input, uint64_t finished)
{
	pthread_mutex_lock(&render.lock);
	frame_pacing_cost_add(&render.costs, input, finished);
	pthread_mutex_unlock(&render.lock);
}

void host_render_stream_pacing_statistics(struct host_render_pacing_statistics *statistics)
{
	pthread_mutex_lock(&render.lock);
	*statistics = render.pacing_statistics;
	statistics->margin = render.costs.margin ? render.costs.margin : FRAME_PACING_MARGIN_NS;
	statistics->budget = frame_pacing_budget(&render.costs, render.period);
	memset(&render.pacing_statistics, 0, sizeof(render.pacing_statistics));
	pthread_mutex_unlock(&render.lock);
}

void host_render_stream_frame_ready(void)
{
	render.frame_ready = monotonic_nanoseconds();
}

void host_render_stream_frame_shown(uint64_t input, uint64_t shown)
{
	frame_pacing_latency_add(&latency, input, shown);
}

uint32_t host_render_stream_latency(float *median, float *high)
{
	return frame_pacing_latency_take(&latency, median, high);
}

/* sleeps until a time of CLOCK_MONOTONIC. On Apple's systems a thread's
timed waits can end tens of milliseconds late (the system coalesces timers:
5 ms slept 39 on an M1 Max with the person away), but not a time constraint
thread's: the thread is one while it waits, and standard again after */
static void sleep_until(uint64_t when)
{
#ifdef __APPLE__
	static mach_timebase_info_data_t timebase;
	thread_port_t thread = pthread_mach_thread_np(pthread_self());
	thread_time_constraint_policy_data_t constraint;
	thread_standard_policy_data_t standard;
	uint64_t now = monotonic_nanoseconds();

	if (when <= now)
		return;
	if (!timebase.denom)
		mach_timebase_info(&timebase);
	/* (a refresh's period, a little of it computing, woken within a
	millisecond: in the time base's units) */
	constraint.period = (uint32_t)(8333333ull * timebase.denom / timebase.numer);
	constraint.computation = (uint32_t)(500000ull * timebase.denom / timebase.numer);
	constraint.constraint = (uint32_t)(1000000ull * timebase.denom / timebase.numer);
	constraint.preemptible = 1;
	thread_policy_set(thread, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&constraint,
		THREAD_TIME_CONSTRAINT_POLICY_COUNT);
	mach_wait_until(mach_absolute_time() + (when - now) * timebase.denom / timebase.numer);
	memset(&standard, 0, sizeof(standard));
	thread_policy_set(thread, THREAD_STANDARD_POLICY, (thread_policy_t)&standard, THREAD_STANDARD_POLICY_COUNT);
#else
	struct timespec until;

	until.tv_sec = (time_t)(when / 1000000000ull);
	until.tv_nsec = (long)(when % 1000000000ull);
	while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &until, NULL) == EINTR)
		;
#endif
}

void host_render_stream_sleep_until(uint64_t when)
{
	sleep_until(when);
}

/* (render.lock held) the game waits until the display has taken its frames
but FRAME_PACING_QUEUED (FRAME_PACING_WAIT_MAXIMUM_NS at most: a display
that takes none must not stop the game), then until its frame's start
time, its budget before the latch the frame is for */
static void display_wait(uint64_t *start)
{
	struct timespec until;
	uint64_t start_time, now, budget, extra;

	if (!render.display_paced)
		return;
	clock_gettime(CLOCK_REALTIME, &until);
	until.tv_sec += (time_t)(FRAME_PACING_WAIT_MAXIMUM_NS / 1000000000ull);
	until.tv_nsec += (long)(FRAME_PACING_WAIT_MAXIMUM_NS % 1000000000ull);
	if (until.tv_nsec >= 1000000000L)
	{
		until.tv_sec++;
		until.tv_nsec -= 1000000000L;
	}
	while (!render.ring->error && render.display_paced &&
		!frame_pacing_may_start(render.ring->frames_submitted, render.frames_taken, FRAME_PACING_QUEUED))
	{
		if (!*start)
		{
			*start = monotonic_nanoseconds();
			render.ring->waits++;
		}
		if (pthread_cond_timedwait(&render.done, &render.lock, &until) == ETIMEDOUT)
		{
			/* (the display takes no frames: those it has not taken are
			counted taken, so that the game goes on) */
			render.frames_taken = render.ring->frames_submitted - FRAME_PACING_QUEUED;
			break;
		}
	}
	if (render.ring->error || !render.display_paced)
		return;
	/* (and the time the guest says the next frame takes more than its
	frames do, a load zone's switch: its start so much earlier, within a
	refresh) */
	extra = (uint64_t)__atomic_load_n(&render.ring->frame_extra_microseconds, __ATOMIC_RELAXED) * 1000ull;
	budget = frame_pacing_budget(&render.costs, render.period) + (extra < render.period ? extra : render.period);
	start_time = frame_pacing_start_time(render.latch, render.period,
		render.ring->frames_submitted - render.frames_taken, budget);
	now = monotonic_nanoseconds();
	/* (the refresh the next frame is for: host_render_stream_frame_displayed
	tells a miss by it) */
	{
		uint32_t next = render.ring->frames_submitted + 1;

		render.targets[next % 8].frame = next;
		render.targets[next % 8].target = start_time ? start_time + budget : 0;
		render.targets[next % 8].waited = start_time > now && start_time - now < 4 * render.period;
		render.targets[next % 8].margin = render.costs.margin ? render.costs.margin : FRAME_PACING_MARGIN_NS;
		if (start_time && !render.targets[next % 8].waited)
			render.pacing_statistics.late_starts++;
	}
	/* (a start further off than a few refreshes is a stale latch: the
	display has stopped) */
	if (start_time > now && start_time - now < 4 * render.period)
	{
		if (!*start)
		{
			*start = now;
			render.ring->waits++;
		}
		pthread_mutex_unlock(&render.lock);
		sleep_until(start_time);
		pthread_mutex_lock(&render.lock);
	}
}

/* whether the game waits for the render thread, or will: a guest waiting,
or records committed and not yet played */
static int loading_stream_waits(void)
{
	struct xgpu_stream_ring *ring = render.ring;

	if (__atomic_load_n(&render.guest_waiting, __ATOMIC_RELAXED))
		return 1;
	return ring && __atomic_load_n(&ring->committed, __ATOMIC_ACQUIRE) !=
		__atomic_load_n(&ring->decoded, __ATOMIC_ACQUIRE);
}

static void guest_wait(uint64_t *start)
{
	if (!*start)
	{
		*start = monotonic_nanoseconds();
		render.ring->waits++;
	}
	/* (read by the render thread's loading picture without the lock) */
	__atomic_add_fetch(&render.guest_waiting, 1, __ATOMIC_RELAXED);
	pthread_cond_signal(&render.work);
	pthread_cond_wait(&render.done, &render.lock);
	__atomic_sub_fetch(&render.guest_waiting, 1, __ATOMIC_RELAXED);
}

static void guest_waited(uint64_t start)
{
	if (start)
		render.ring->wait_nanoseconds += (uint32_t)(monotonic_nanoseconds() - start);
}

void host_render_stream_submit(uint32_t flags)
{
	struct xgpu_stream_ring *ring = render.ring;
	uint64_t start = 0;

	if (!render.running)
		return;
	pthread_mutex_lock(&render.lock);
	render.committed = __atomic_load_n(&ring->committed, __ATOMIC_ACQUIRE);
	if (flags & XGPU_STREAM_SUBMIT_FRAME)
		render.frame_submitted[++render.frames_committed % 8] = monotonic_nanoseconds();
	pthread_cond_signal(&render.work);
	while (!ring->error)
	{
		if ((flags & XGPU_STREAM_SUBMIT_FRAME) && ring->frames_submitted - render.retired_frames > ring->frames_in_flight)
			guest_wait(&start);
		else if ((flags & XGPU_STREAM_SUBMIT_SYNC) && (int32_t)(ring->released - render.committed) < 0)
			guest_wait(&start);
		else
			break;
	}
	if (flags & XGPU_STREAM_SUBMIT_FRAME)
		display_wait(&start);
	guest_waited(start);
	pthread_mutex_unlock(&render.lock);
}

void host_render_stream_wait(uint32_t position)
{
	struct xgpu_stream_ring *ring = render.ring;
	uint64_t start = 0;

	if (!render.running)
		return;
	pthread_mutex_lock(&render.lock);
	while (!ring->error && (int32_t)(ring->released - position) < 0)
		guest_wait(&start);
	guest_waited(start);
	pthread_mutex_unlock(&render.lock);
}

void host_render_stream_call(void (*function)(void *argument), void *argument)
{
	uint64_t run;

	if (!render.running)
	{
		function(argument);
		return;
	}
	pthread_mutex_lock(&render.lock);
	while (render.call && !render.ring->error)
		pthread_cond_wait(&render.done, &render.lock);
	if (render.ring->error)
	{
		/* (no records are played after a malformed one: nor is this) */
		pthread_mutex_unlock(&render.lock);
		host_logf(HOST_LOG_ERROR, "render stream: a call for the render thread after the stream failed");
		return;
	}
	render.call = function;
	render.call_argument = argument;
	render.call_position = render.committed;
	run = render.calls_run;
	pthread_cond_signal(&render.work);
	while (render.calls_run == run && !render.ring->error)
		pthread_cond_wait(&render.done, &render.lock);
	pthread_mutex_unlock(&render.lock);
}

const struct xgpu_stream_ring *host_render_stream_ring(void)
{
	return render.running ? render.ring : NULL;
}

void host_render_stream_exiting(void)
{
	if (render_backend && render_backend->exiting)
		render_backend->exiting(render_backend->context);
}

void host_render_stream_close(void)
{
	if (!render.running)
		return;
	pthread_mutex_lock(&render.lock);
	render.stopping = 1;
	pthread_cond_signal(&render.work);
	pthread_mutex_unlock(&render.lock);
	pthread_join(render.thread, NULL);
	host_render_record_stop();
	host_render_decoder_free(render.decoder);
	render.decoder = NULL;
	render.running = 0;
}
