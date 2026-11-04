/*
HOST_RENDER_STREAM_TEST.C

Tests of the renderer's command stream (port/linux/src/xgpu_stream.h): the
guest's encoder (xgpu_stream_encode.c) and the host's decoder and render
thread (port/android/host/host_render_stream.c), built together on the
build machine (tools/test_render_stream.py), the guest's memory a 4 GB
window at a base as with the base register.

The front end's calls are random but repeatable; each test writes what the
front end asked for (at each draw, the whole state it draws with) and the
backend writes what it was given, and the two must be the same. The
backend's GPU is a thread that finishes each fence a while after it is
made, and checks then that the data the fence's draws read in the ring is
still what it was: the ring's bytes are not written again before the GPU
is done with them.
*/

#include "xgpu_backend.h"
#include "xgpu_shader.h"
#include "host.h"
#include "host_render_stream.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* the encoder's (xgpu_stream_encode.c) */
void xgpu_stream_encode_configure(unsigned long ring_size, unsigned long frames_in_flight);
const struct xgpu_stream_ring *xgpu_stream_encode_ring(void);
void xgpu_stream_encode_close(void);

static int failures;

#define CHECK(condition) do { if (!(condition)) { failures++; \
	fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); } } while (0)

/* ---------- the guest's memory */

uintptr_t host_guest_base;
static uintptr_t window_next, window_end;

#define WINDOW_SIZE (512ul << 20)

static void window_make(void)
{
	size_t reserve = 8ul << 30;
	unsigned char *reserved = mmap(NULL, reserve, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	uintptr_t base;

	if (reserved == MAP_FAILED)
	{
		perror("mmap");
		exit(1);
	}
	base = ((uintptr_t)reserved + (1ul << 32) - 1) & ~((1ul << 32) - 1);
	if (mprotect((void *)base, WINDOW_SIZE, PROT_READ | PROT_WRITE))
	{
		perror("mprotect");
		exit(1);
	}
	host_guest_base = base;
	/* (guest address 0 is NULL) */
	window_next = base + 0x10000;
	window_end = base + WINDOW_SIZE;
}

void *xgpu_stream_test_allocate(size_t alignment, size_t size)
{
	uintptr_t address = (window_next + alignment - 1) & ~(uintptr_t)(alignment - 1);

	if (address + size > window_end)
		return NULL;
	window_next = address + size;
	return memset((void *)address, 0, size);
}

static void *guest_allocate(size_t size)
{
	return xgpu_stream_test_allocate(16, size);
}

/* ---------- what the encoder calls besides the host (the front end's
own code in the game) */

void platform_log(const char *format, ...)
{
	(void)format;
}

/* the floats as the encoder sends them: each not a number, or infinite, 0
(xgpu_stream_encode.c's stream_finite) */
static void finite_floats(float *values, size_t count)
{
	size_t index;

	for (index = 0; index < count; index++)
	{
		uint32_t bits;

		memcpy(&bits, &values[index], sizeof(bits));
		if ((bits & 0x7f800000u) == 0x7f800000u)
			values[index] = 0.0f;
	}
}

void host_logf(int priority, const char *format, ...)
{
	va_list arguments;

	(void)priority;
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
}

void xgpu_texture_describe(DWORD format_word, DWORD size_word, struct xgpu_texture_description *description)
{
	memset(description, 0, sizeof(*description));
	description->format = format_word & 0xff;
	description->width = (size_word & 0xfff) + 1;
	description->height = ((size_word >> 12) & 0xfff) + 1;
	description->depth = 1;
	description->levels = 1;
	description->cube_map = (format_word >> 8) & 1;
}

/* the IR made from the key (any function of it does) */
void nv2a_pixel_program_decode(const struct nv2a_pixel_shader_key *key, struct xgpu_pixel_program *program)
{
	unsigned char *bytes = (unsigned char *)program;
	size_t index;

	memset(program, 0, sizeof(*program));
	for (index = 0; index < sizeof(*program); index++)
		bytes[index] = (unsigned char)(key->combiner_state[index % D3DRS_PS_MAX] >> (index % 4 * 8));
	program->combiner_count = key->combiner_state[3] & 7;
	program->alpha_test = key->alpha_test_function;
}

/* ---------- text */

struct text
{
	char *data;
	size_t length, capacity;
};

static void text_printf(struct text *text, const char *format, ...) __attribute__((format(printf, 2, 3)));

static void text_printf(struct text *text, const char *format, ...)
{
	va_list arguments;
	int length;

	for (;;)
	{
		size_t room = text->capacity - text->length;

		va_start(arguments, format);
		length = vsnprintf(text->data ? text->data + text->length : NULL, room, format, arguments);
		va_end(arguments);
		if ((size_t)length < room)
			break;
		text->capacity = text->capacity * 2 + (size_t)length + 4096;
		text->data = realloc(text->data, text->capacity);
	}
	text->length += (size_t)length;
}

static void text_clear(struct text *text)
{
	text->length = 0;
	if (text->data)
		text->data[0] = 0;
}

/* the first line where they differ; 1 if they are the same */
static int texts_compare(const char *what, const struct text *expected, const struct text *actual)
{
	const char *a = expected->data ? expected->data : "", *b = actual->data ? actual->data : "";
	int line = 1;

	if (expected->length == actual->length && !memcmp(a, b, expected->length))
		return 1;
	const char *line_start = a;

	while (*a && *a == *b)
	{
		if (*a == '\n')
		{
			line++;
			line_start = a + 1;
		}
		a++;
		b++;
	}
	/* (the line's start and the 60 characters before where they differ) */
	{
		size_t back = (size_t)(a - line_start) < 60 ? (size_t)(a - line_start) : 60;

		fprintf(stderr, "%s: the logs differ at line %d (%.*s ...):\n  expected: ...%.*s\n  actual:   ...%.*s\n", what,
			line, (int)strcspn(line_start, " \n"), line_start, (int)strcspn(a - back, "\n") < 160 ?
			(int)strcspn(a - back, "\n") : 160, a - back, (int)strcspn(b - back, "\n") < 160 ?
			(int)strcspn(b - back, "\n") : 160, b - back);
	}
	return 0;
}

static uint32_t checksum(const void *data, size_t size)
{
	const unsigned char *bytes = data;
	uint32_t hash = 2166136261u;
	size_t index;

	for (index = 0; index < size; index++)
		hash = (hash ^ bytes[index]) * 16777619u;
	return hash;
}

static uint32_t float_bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

/* ---------- printing the state a draw draws with, from either side's
structures (their fields are named alike) */

#define KEY_BYTES(X) X(immediate) X(lighting) X(views) X(blend) X(blend_source) X(blend_destination) \
	X(blend_operation) X(color_mask) X(depth_test) X(depth_function) X(depth_write) X(stencil_test) \
	X(stencil_function) X(stencil_fail) X(stencil_depth_fail) X(stencil_pass) X(cull) X(front_counter_clockwise) \
	X(cull_front) X(fill) X(depth_bias) X(color_format) X(depth_format) X(samples)
#define PIXEL_BYTES(X) X(fog_enable) X(fog_table_mode) X(count_samples) X(coverage_alpha) X(point_threshold) \
	X(per_pixel_lighting) X(alpha_test_samples) X(multiview) X(source_blend) X(dynamic_lights)
#define PRINT_FIELD(name) text_printf(t, " %lu", (unsigned long)k->name);
#define PRINT_PIXEL_FIELD(name) text_printf(t, " %lu", (unsigned long)k->pixel.name);

#define PRINT_KEY(t, k, program) do { \
	int i_; \
	text_printf(t, " key %016llx %lu %lu %lu", (unsigned long long)(program), (unsigned long)k->packed_attributes, \
		(unsigned long)k->stencil_read_mask, (unsigned long)k->stencil_write_mask); \
	KEY_BYTES(PRINT_FIELD) \
	for (i_ = 0; i_ < D3DRS_PS_MAX; i_++) \
		text_printf(t, " %lx", (unsigned long)k->pixel.combiner_state[i_]); \
	text_printf(t, " %lx %lu", (unsigned long)k->pixel.texture_modes, (unsigned long)k->pixel.alpha_test_function); \
	for (i_ = 0; i_ < 4; i_++) \
		text_printf(t, " %u %u %u", k->pixel.sampler_type[i_], k->pixel.alpha_kill[i_], k->pixel.color_sign[i_]); \
	PIXEL_BYTES(PRINT_PIXEL_FIELD) \
} while (0)

#define PRINT_DYNAMIC(t, d) do { \
	int i_; \
	text_printf(t, " dyn"); \
	for (i_ = 0; i_ < 4; i_++) \
		text_printf(t, " %ld %ld %x", (long)d->viewport[i_], (long)d->scissor[i_], float_bits(d->blend_color[i_])); \
	text_printf(t, " %x %x %lu %x %x", float_bits(d->depth_range[0]), float_bits(d->depth_range[1]), \
		(unsigned long)d->stencil_reference, float_bits(d->depth_bias[0]), float_bits(d->depth_bias[1])); \
} while (0)

#define PRINT_SAMPLER(t, s) text_printf(t, " %u %u %u %u %u %u %x %lu %lu %lx", s.minify, s.mip, s.magnify, \
	s.address[0], s.address[1], s.address[2], float_bits(s.lod_bias), (unsigned long)s.first_level, \
	(unsigned long)s.anisotropy, (unsigned long)s.border_color)

/* the expected side: what the front end last set */
static struct
{
	BOOL keyed;
	struct xgpu_pipeline_key key;
	BOOL stated;
	struct xgpu_dynamic_state dynamic;
	uint32_t uniforms_checksum;
	unsigned long uniforms_serial;
	BOOL textured;
	struct xgpu_texture_source stages[4];
	uint32_t stage_format[4], stage_size[4];
	BOOL vertexed;
	struct xgpu_vertex_input input;
	uint32_t stream_checksum[16];
	float attributes[16][4];
} front;

static struct text expected, actual;

static void print_front_state(struct text *t)
{
	int stage, s, reg;
	uint32_t fed = 0;
	unsigned long index;

	if (front.keyed)
	{
		const struct xgpu_pipeline_key *k = &front.key;
		struct xgpu_pixel_program program;

		PRINT_KEY(t, k, k->vertex_program);
		nv2a_pixel_program_decode(&k->pixel, &program);
		text_printf(t, " ir %lu %lu", (unsigned long)program.combiner_count, (unsigned long)program.alpha_test);
	}
	if (front.stated)
	{
		const struct xgpu_dynamic_state *d = &front.dynamic;

		PRINT_DYNAMIC(t, d);
		text_printf(t, " uniforms %lu %08x", front.uniforms_serial, front.uniforms_checksum);
	}
	if (front.textured)
	{
		for (stage = 0; stage < 4; stage++)
		{
			const struct xgpu_texture_source *source = &front.stages[stage];
			unsigned long levels = source->rendered_levels < XGPU_MIP_LEVELS ? source->rendered_levels : XGPU_MIP_LEVELS;

			text_printf(t, " tex%d %u %u %u %x %x %x %x %u %u %x %lu %lu %lu %lu", stage, source->source,
				source->sampler_type, source->sampled, (uint32_t)source->resource, (uint32_t)source->palette,
				front.stage_format[stage], front.stage_size[stage], source->target, source->drawn,
				(uint32_t)source->data, source->width, source->height, source->levels, source->rendered_levels);
			PRINT_SAMPLER(t, source->sampler);
			for (index = 0; index < levels; index++)
				text_printf(t, " L%u/%lu", source->level_targets[index], source->level_written[index]);
		}
	}
	if (front.vertexed)
	{
		const struct xgpu_vertex_input *input = &front.input;

		text_printf(t, " layout %lu %lu %lx", input->declaration, input->element_count, input->packed_mask);
		for (index = 0; index < input->element_count; index++)
		{
			const struct xgpu_vertex_element *element = &input->elements[index];

			text_printf(t, " e%u.%u.%u.%u.%u", element->reg, element->stream, element->format, element->bytes,
				element->offset);
			fed |= 1u << element->reg;
		}
		for (s = 0; s < 16; s++)
		{
			if (!input->streams[s].present)
				continue;
			text_printf(t, " s%d %ld %lu %lu %lu", s, input->streams[s].segment, input->streams[s].offset,
				input->streams[s].bytes, input->streams[s].stride);
			if (input->streams[s].segment < 0)
				text_printf(t, " %08x", front.stream_checksum[s]);
		}
		for (reg = 0; reg < 16; reg++)
		{
			if (!(fed & (1u << reg)))
				text_printf(t, " a%d %08x", reg, checksum(front.input.attributes[reg], 16));
		}
	}
}

/* ---------- the test backend */

struct check
{
	const void *data;
	uint32_t size;
	uint32_t checksum;
};

#define CHECKS 8192

static struct
{
	/* what it is given */
	BOOL keyed;
	struct xgpu_stream_pipeline_define key;
	BOOL stated;
	struct xgpu_stream_dynamic_state dynamic;
	struct xgpu_stream_draw_uniforms uniforms;
	uint32_t uniforms_serial;
	BOOL textured;
	struct host_render_texture_stage stages[4];
	BOOL vertexed;
	struct host_render_vertex_input input;
	uint32_t targets[4096][2];
	volatile uint32_t *visibility;
	unsigned char *ring;
	uint32_t ring_size;

	/* the GPU: fences made, done; the data read since the last fence, and
	each fence's */
	pthread_mutex_t lock;
	pthread_cond_t changed;
	pthread_t thread;
	BOOL stopping;
	BOOL gpu_held;
	uint64_t fences_made, fences_done, fence_checked;
	unsigned gpu_microseconds;
	struct check pending[CHECKS];
	uint32_t pending_count;
	struct
	{
		struct check *checks;
		uint32_t count;
	} fence_checks[256];
	int check_failures;
	unsigned fails_vertex_program;
	int frames_fenced;
} backend_state =
{
	.lock = PTHREAD_MUTEX_INITIALIZER,
	.changed = PTHREAD_COND_INITIALIZER,
};

static void read_data(const void *data, uint32_t size)
{
	if (!size || backend_state.pending_count == CHECKS)
		return;
	backend_state.pending[backend_state.pending_count].data = data;
	backend_state.pending[backend_state.pending_count].size = size;
	backend_state.pending[backend_state.pending_count].checksum = checksum(data, size);
	backend_state.pending_count++;
}

static void print_backend_state(struct text *t)
{
	int stage, s, reg;
	uint32_t fed = 0;
	uint32_t index;

	if (backend_state.keyed)
	{
		const struct xgpu_stream_pipeline_key *k = &backend_state.key.key;

		PRINT_KEY(t, k, ((unsigned long long)k->vertex_program[1] << 32) | k->vertex_program[0]);
		text_printf(t, " ir %lu %lu", (unsigned long)backend_state.key.pixel_program.combiner_count,
			(unsigned long)backend_state.key.pixel_program.alpha_test);
	}
	if (backend_state.stated)
	{
		const struct xgpu_stream_dynamic_state *d = &backend_state.dynamic;

		PRINT_DYNAMIC(t, d);
		text_printf(t, " uniforms %lu %08x", (unsigned long)backend_state.uniforms_serial,
			checksum(&backend_state.uniforms, sizeof(backend_state.uniforms)));
	}
	if (backend_state.textured)
	{
		for (stage = 0; stage < 4; stage++)
		{
			const struct xgpu_stream_texture_stage *source = &backend_state.stages[stage].stage;

			text_printf(t, " tex%d %u %u %u %x %x %x %x %u %u %x %lu %lu %lu %lu", stage, source->source,
				source->sampler_type, source->sampled, source->resource, source->palette, source->resource_format,
				source->resource_size, source->target, source->drawn, source->data, (unsigned long)source->width,
				(unsigned long)source->height, (unsigned long)source->levels, (unsigned long)source->rendered_levels);
			PRINT_SAMPLER(t, source->sampler);
			for (index = 0; index < source->level_count; index++)
				text_printf(t, " L%u/%lu", backend_state.stages[stage].levels[index].target,
					(unsigned long)backend_state.stages[stage].levels[index].written);
		}
	}
	if (backend_state.vertexed)
	{
		const struct host_render_vertex_input *input = &backend_state.input;

		text_printf(t, " layout %lu %lu %lx", (unsigned long)input->layout->declaration,
			(unsigned long)input->layout->element_count, (unsigned long)input->layout->packed_mask);
		for (index = 0; index < input->layout->element_count; index++)
		{
			const struct xgpu_stream_vertex_element *element = &input->layout->elements[index];

			text_printf(t, " e%u.%u.%u.%u.%u", element->reg, element->stream, element->format, element->bytes,
				element->offset);
			fed |= 1u << element->reg;
		}
		for (s = 0; s < 16; s++)
		{
			const struct xgpu_stream_vertex_stream *stream = &input->streams[s];

			if (!(input->present & (1u << s)))
				continue;
			text_printf(t, " s%d %ld %lu %lu %lu", s, (long)stream->segment, (unsigned long)stream->offset,
				(unsigned long)stream->bytes, (unsigned long)stream->stride);
			if (stream->segment < 0)
			{
				text_printf(t, " %08x", checksum(host_pointer(stream->data), stream->bytes));
				read_data(host_pointer(stream->data), stream->bytes);
			}
		}
		for (reg = 0; reg < 16; reg++)
		{
			if (!(fed & (1u << reg)))
				text_printf(t, " a%d %08x", reg, checksum(input->attributes[reg], 16));
		}
	}
}

static void *gpu_thread(void *argument)
{
	(void)argument;
	pthread_mutex_lock(&backend_state.lock);
	while (!backend_state.stopping)
	{
		if (!backend_state.gpu_held && backend_state.fences_done < backend_state.fences_made)
		{
			uint64_t fence = backend_state.fences_done + 1;
			uint32_t index;

			pthread_mutex_unlock(&backend_state.lock);
			usleep(backend_state.gpu_microseconds);
			pthread_mutex_lock(&backend_state.lock);
			/* the draws read their data now: still what it was */
			for (index = 0; index < backend_state.fence_checks[fence % 256].count; index++)
			{
				const struct check *check = &backend_state.fence_checks[fence % 256].checks[index];

				if (checksum(check->data, check->size) != check->checksum)
					backend_state.check_failures++;
			}
			free(backend_state.fence_checks[fence % 256].checks);
			backend_state.fence_checks[fence % 256].checks = NULL;
			backend_state.fences_done = fence;
			pthread_cond_broadcast(&backend_state.changed);
			pthread_mutex_unlock(&backend_state.lock);
			host_render_stream_gpu_completed();
			pthread_mutex_lock(&backend_state.lock);
			continue;
		}
		pthread_cond_wait(&backend_state.changed, &backend_state.lock);
	}
	pthread_mutex_unlock(&backend_state.lock);
	return NULL;
}

static void test_open(void *context, void *ring, uint32_t ring_size, volatile uint32_t *visibility)
{
	(void)context;
	backend_state.ring = ring;
	backend_state.ring_size = ring_size;
	backend_state.visibility = visibility;
}

static uint64_t test_fence(void *context)
{
	uint64_t fence;

	(void)context;
	pthread_mutex_lock(&backend_state.lock);
	/* (no more than 256 unfinished: the render thread waits for the GPU
	itself before) */
	while (backend_state.fences_made - backend_state.fences_done >= 255)
		pthread_cond_wait(&backend_state.changed, &backend_state.lock);
	fence = ++backend_state.fences_made;
	backend_state.fence_checks[fence % 256].count = backend_state.pending_count;
	backend_state.fence_checks[fence % 256].checks = malloc(sizeof(struct check) * (backend_state.pending_count + 1));
	memcpy(backend_state.fence_checks[fence % 256].checks, backend_state.pending,
		sizeof(struct check) * backend_state.pending_count);
	backend_state.pending_count = 0;
	pthread_cond_broadcast(&backend_state.changed);
	pthread_mutex_unlock(&backend_state.lock);
	return fence;
}

static void test_wait(void *context, uint64_t fence)
{
	(void)context;
	pthread_mutex_lock(&backend_state.lock);
	backend_state.fence_checked = fence;
	pthread_cond_broadcast(&backend_state.changed);
	while (backend_state.fences_done < fence)
		pthread_cond_wait(&backend_state.changed, &backend_state.lock);
	pthread_mutex_unlock(&backend_state.lock);
}

static int test_fence_ready(void *context, uint64_t fence)
{
	int ready;

	(void)context;
	pthread_mutex_lock(&backend_state.lock);
	backend_state.fence_checked = fence;
	ready = backend_state.fences_done >= fence;
	pthread_cond_broadcast(&backend_state.changed);
	pthread_mutex_unlock(&backend_state.lock);
	return ready;
}

static void test_initialize(void *context, struct xgpu_stream_capabilities *capabilities)
{
	(void)context;
	capabilities->largest_target = 16384;
	capabilities->most_samples = 8;
	capabilities->most_anisotropy = 16;
	capabilities->sample_counting = 1;
	capabilities->bgra_vertex_colors = 1;
	capabilities->base_vertex = 1;
	capabilities->dynamic_lights = 1;
	capabilities->dynamic_lights_preparable = 1;
	text_printf(&actual, "initialize\n");
}

static void test_invalidate(void *context)
{
	(void)context;
	text_printf(&actual, "invalidate\n");
}

static void test_flush(void *context)
{
	(void)context;
	text_printf(&actual, "flush\n");
}

static void test_target_create(void *context, const struct xgpu_stream_target_create *record)
{
	(void)context;
	if (record->target < 4096)
	{
		backend_state.targets[record->target][0] = record->width;
		backend_state.targets[record->target][1] = record->height;
	}
	text_printf(&actual, "target %u %u %u %u %u\n", record->target, record->width, record->height, record->depth,
		record->layers);
}

static void test_target_delete(void *context, uint32_t target)
{
	(void)context;
	text_printf(&actual, "target delete %u\n", target);
}

static void test_set_targets(void *context, const struct xgpu_stream_set_targets *record)
{
	(void)context;
	text_printf(&actual, "targets %u %d %u %d %u %u\n", record->color, record->hud_group, record->depth, record->views,
		record->base, record->samples);
}

static void test_clear(void *context, const struct xgpu_stream_clear *record)
{
	uint32_t index;

	(void)context;
	text_printf(&actual, "clear %x %x %x %u", record->flags, float_bits(record->color[1]), float_bits(record->depth),
		record->stencil);
	for (index = 0; index < record->count; index++)
		text_printf(&actual, " %d,%d,%d,%d", record->rectangles[index][0], record->rectangles[index][1],
			record->rectangles[index][2], record->rectangles[index][3]);
	text_printf(&actual, "\n");
}

static int test_read_target(void *context, const struct xgpu_stream_read_target *record)
{
	unsigned char *pixels = host_pointer(record->pixels);
	uint32_t index;

	(void)context;
	for (index = 0; index < record->size; index++)
		pixels[index] = (unsigned char)(index * 7 + record->target);
	text_printf(&actual, "read %u %d %u %u\n", record->target, record->layer, record->tone_map, record->size);
	return 1;
}

/* the screenshot key's file (XGPU_STREAM_SCREENSHOT): what the host's
writer was given */
static struct
{
	int calls;
	uint32_t width, height;
	char directory[256];
	int pixels_read;
} screenshot_test;

static int test_screenshot_writer(unsigned char *bgra, uint32_t width, uint32_t height, const char *directory, char *name,
	uint32_t name_size)
{
	uint32_t index;

	screenshot_test.calls++;
	screenshot_test.width = width;
	screenshot_test.height = height;
	snprintf(screenshot_test.directory, sizeof(screenshot_test.directory), "%s", directory);
	screenshot_test.pixels_read = 1;
	for (index = 0; index < width * height * 4; index += 997)
	{
		if (bgra[index] != (unsigned char)(index * 7 + 7))
			screenshot_test.pixels_read = 0;
	}
	if (!strcmp(directory, "/no/such/folder"))
	{
		snprintf(name, name_size, "the screenshots folder cannot be made");
		return 0;
	}
	snprintf(name, name_size, "2026-01-02_03.04.05.png");
	return 1;
}

static void test_set_textures(void *context, const struct host_render_texture_stage stages[4])
{
	(void)context;
	memcpy(backend_state.stages, stages, sizeof(backend_state.stages));
	backend_state.textured = TRUE;
}

static void test_set_constants(void *context, const struct xgpu_stream_constants *record)
{
	(void)context;
	text_printf(&actual, "constants %u %u %08x\n", record->first, record->count,
		checksum(record->values, record->count * 16));
}

static void test_vertex_program(void *context, const struct xgpu_stream_vertex_program *record)
{
	(void)context;
	text_printf(&actual, "program %u %08x%08x %u %u %08x\n", record->id, record->hash[1], record->hash[0],
		record->instruction_count, record->projects,
		checksum(record->instructions, record->instruction_count * XGPU_STREAM_VERTEX_INSTRUCTION_SIZE));
}

static int test_pipeline(void *context, uint32_t id, const struct xgpu_stream_pipeline_define *definition)
{
	(void)context;
	(void)id;
	backend_state.key = *definition;
	backend_state.keyed = TRUE;
	return definition->key.vertex_program[0] % 7 != backend_state.fails_vertex_program;
}

static void test_draw_state(void *context, const struct xgpu_stream_dynamic_state *dynamic,
	const struct xgpu_stream_draw_uniforms *uniforms, uint32_t uniforms_serial)
{
	(void)context;
	backend_state.dynamic = *dynamic;
	backend_state.uniforms = *uniforms;
	backend_state.uniforms_serial = uniforms_serial;
	backend_state.stated = TRUE;
}

static void test_mirror_write(void *context, const struct xgpu_stream_mirror_write *record)
{
	(void)context;
	text_printf(&actual, "mirror %u %u %u %u %08x\n", record->segment, record->offset, record->size, record->unused,
		checksum(host_pointer(record->data), record->size));
	read_data(host_pointer(record->data), record->size);
}

static void test_set_vertices(void *context, const struct host_render_vertex_input *input)
{
	(void)context;
	backend_state.input = *input;
	backend_state.vertexed = TRUE;
}

static void test_draw(void *context, const struct xgpu_stream_draw *record)
{
	(void)context;
	text_printf(&actual, "draw %u %u", record->primitive, record->count);
	print_backend_state(&actual);
	text_printf(&actual, "\n");
}

static void test_draw_indices(void *context, const struct xgpu_stream_draw_indices *record)
{
	(void)context;
	text_printf(&actual, "draw indices %u %u %08x", record->primitive, record->count,
		checksum(host_pointer(record->indices), record->count * 2));
	read_data(host_pointer(record->indices), record->count * 2);
	print_backend_state(&actual);
	text_printf(&actual, "\n");
}

static void test_draw_indexed(void *context, const struct xgpu_stream_draw_indexed *record)
{
	(void)context;
	text_printf(&actual, "draw indexed %u %u %u %d %u %u", record->primitive, record->count, record->minimum,
		record->segment, record->offset, record->temporary);
	if (record->segment < 0)
	{
		text_printf(&actual, " %08x", checksum(host_pointer(record->indices), record->count * 2));
		read_data(host_pointer(record->indices), record->count * 2);
	}
	print_backend_state(&actual);
	text_printf(&actual, "\n");
}

static void test_draw_immediate(void *context, const struct xgpu_stream_draw_immediate *record,
	const float (*attributes)[4])
{
	uint32_t vertex_bytes = record->count * (uint32_t)__builtin_popcount(record->written) * 16;
	int reg;

	(void)context;
	text_printf(&actual, "immediate %u %u %x %08x %u", record->primitive, record->count, record->written,
		checksum(host_pointer(record->vertices), vertex_bytes), record->index_count);
	read_data(host_pointer(record->vertices), vertex_bytes);
	if (record->index_count)
		text_printf(&actual, " %08x", checksum(host_pointer(record->indices), record->index_count * 2));
	for (reg = 0; reg < 16; reg++)
	{
		if (!(record->written & (1u << reg)))
			text_printf(&actual, " a%d %08x", reg, checksum(attributes[reg], 16));
	}
	if (backend_state.keyed)
	{
		const struct xgpu_stream_pipeline_key *k = &backend_state.key.key;
		struct text *t = &actual;

		PRINT_KEY(t, k, ((unsigned long long)k->vertex_program[1] << 32) | k->vertex_program[0]);
	}
	text_printf(&actual, "\n");
}

static void test_visibility_begin(void *context)
{
	(void)context;
	text_printf(&actual, "visibility begin\n");
}

static void test_visibility_end(void *context, const struct xgpu_stream_visibility_end *record)
{
	(void)context;
	text_printf(&actual, "visibility end %u %x %d %d %u\n", record->slot, float_bits(record->area), record->views,
		record->samples, record->test);
	__atomic_store_n(&backend_state.visibility[record->slot], record->slot * 3 + 1, __ATOMIC_RELAXED);
	__atomic_store_n(&backend_state.visibility[XGPU_STREAM_VISIBILITY_SLOTS + record->slot], record->test,
		__ATOMIC_RELEASE);
}

static void test_present(void *context, uint32_t back_buffer)
{
	(void)context;
	text_printf(&actual, "present %u\n", back_buffer);
}

/* a display that takes each frame as it ends, its refreshes this far apart
(test_display_paced; 0: none) */
static uint64_t display_period;

static uint64_t test_now(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
}

static void test_frame_end(void *context)
{
	(void)context;
	text_printf(&actual, "frame end\n");
	if (display_period)
	{
		uint64_t now = test_now();

		host_render_stream_frame_displayed(host_render_stream_frame_input(), now, now, now + 1000000u,
			display_period);
	}
}

static struct
{
	unsigned frames, ready, ended, invalid;
	int slow;
	uint64_t serial, elapsed;
} loading_test;

static unsigned char loading_art_copy[XGPU_STREAM_LOADING_ART_BYTES];
static unsigned loading_art_calls;
static int loading_art_valid;

static void test_loading_art(void *context, const unsigned char *rgba)
{
	(void)context;
	loading_art_calls++;
	loading_art_valid = rgba != NULL;
	if (rgba)
		memcpy(loading_art_copy, rgba, sizeof(loading_art_copy));
	else
		memset(loading_art_copy, 0, sizeof(loading_art_copy));
}

static uint64_t test_loading(void *context, const struct loading_presentation_frame *frame)
{
	(void)context;
	if (!frame->active)
		__atomic_add_fetch(&loading_test.ended, 1, __ATOMIC_RELEASE);
	else if (frame->ready)
	{
		if (host_render_stream_loading_holds())
			__atomic_add_fetch(&loading_test.invalid, 1, __ATOMIC_RELEASE);
		__atomic_add_fetch(&loading_test.ready, 1, __ATOMIC_RELEASE);
	}
	else
	{
		if (loading_test.slow)
			usleep(5000);
		if (!host_render_stream_loading_holds() || frame->count != LOADING_PRESENTATION_RECTS ||
			frame->serial <= loading_test.serial || frame->elapsed_ns < loading_test.elapsed)
			__atomic_add_fetch(&loading_test.invalid, 1, __ATOMIC_RELEASE);
		loading_test.serial = frame->serial;
		loading_test.elapsed = frame->elapsed_ns;
		__atomic_add_fetch(&loading_test.frames, 1, __ATOMIC_RELEASE);
	}
	return 1000000;
}

static void test_dynamic_lights_upload(void *context, const struct xgpu_stream_dynamic_lights_upload *record)
{
	(void)context;
	text_printf(&actual, "lights %08x %08x\n", checksum(&record->block, sizeof(record->block)),
		checksum(host_pointer(record->cells), record->size));
}

static void test_dynamic_lights_halos(void *context, const struct xgpu_stream_dynamic_lights_halos *record)
{
	(void)context;
	text_printf(&actual, "halos %u %u %d %d %08x %08x\n", record->color, record->depth, record->layers, record->count,
		checksum(record->halos, (size_t)record->count * 32), checksum(record->camera_right, 43 * 4));
}

static int test_vr_supported(void *context)
{
	(void)context;
	text_printf(&actual, "vr supported\n");
	return 1;
}

static void test_vr_initialize(void *context, struct xgpu_stream_vr_info *info)
{
	(void)context;
	text_printf(&actual, "vr initialize %d %d %d\n", info->eye_width, info->eye_height, info->scope_size);
	info->foveated = 1;
	info->fovea_width = info->eye_width / 2;
	info->fovea_height = info->eye_height / 2;
}

static void test_vr_pass(void *context, int32_t pass)
{
	(void)context;
	text_printf(&actual, "vr pass %d\n", pass);
}

static void test_vr_submit(void *context, const struct xgpu_stream_vr_submit *record)
{
	const struct xgpu_stream_vr_frame *frame = &record->record.frame;
	uint32_t screenshot = record->record.screenshot;

	(void)context;
	text_printf(&actual, "vr submit %d %u %u %u\n", frame->eyes_drawn, frame->eye_target, frame->hud, screenshot);
}

static const struct host_render_backend test_backend =
{
	.open = test_open,
	.fence = test_fence,
	.wait = test_wait,
	.initialize = test_initialize,
	.invalidate = test_invalidate,
	.flush = test_flush,
	.target_create = test_target_create,
	.target_delete = test_target_delete,
	.set_targets = test_set_targets,
	.clear = test_clear,
	.read_target = test_read_target,
	.set_textures = test_set_textures,
	.set_constants = test_set_constants,
	.vertex_program = test_vertex_program,
	.pipeline = test_pipeline,
	.draw_state = test_draw_state,
	.mirror_write = test_mirror_write,
	.set_vertices = test_set_vertices,
	.draw = test_draw,
	.draw_indices = test_draw_indices,
	.draw_indexed = test_draw_indexed,
	.draw_immediate = test_draw_immediate,
	.visibility_begin = test_visibility_begin,
	.visibility_end = test_visibility_end,
	.present = test_present,
	.frame_end = test_frame_end,
	.loading = test_loading,
	.loading_art = test_loading_art,
	.dynamic_lights_upload = test_dynamic_lights_upload,
	.dynamic_lights_halos = test_dynamic_lights_halos,
	.vr_supported = test_vr_supported,
	.vr_initialize = test_vr_initialize,
	.vr_pass = test_vr_pass,
	.vr_submit = test_vr_submit,
};

/* ---------- the front end: random calls, and what they should come to */

static uint64_t random_state;

static uint32_t random_next(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 7;
	random_state ^= random_state << 17;
	return (uint32_t)(random_state >> 16);
}

static uint32_t random_below(uint32_t limit)
{
	return random_next() % limit;
}

static void random_bytes(void *data, size_t size)
{
	unsigned char *bytes = data;
	size_t index;

	for (index = 0; index < size; index++)
		bytes[index] = (unsigned char)random_next();
}

/* a few of each, as a game has (its keys, layouts, textures) */
#define KEY_POOL 24
#define LAYOUT_POOL 8
#define TEXTURE_POOL 16

static struct
{
	struct xgpu_pipeline_key keys[KEY_POOL];
	struct xgpu_vertex_input layouts[LAYOUT_POOL];
	D3DBaseTexture *textures[TEXTURE_POOL];
	unsigned long uniforms_serial;
	struct xgpu_draw_uniforms uniforms;
	/* the largest streamed data a draw has, and how often a draw is that
	large */
	uint32_t stream_bytes;
	uint32_t large_every;
	uint32_t large_bytes;
	/* the most vertices an immediate draw has */
	uint32_t immediate_vertices;
} pool;

static void pool_make(void)
{
	int index, element;

	for (index = 0; index < KEY_POOL; index++)
	{
		struct xgpu_pipeline_key *key = &pool.keys[index];

		memset(key, 0, sizeof(*key));
		key->vertex_program = ((unsigned long long)random_next() << 32) | (unsigned long long)random_next();
		key->packed_attributes = random_below(4);
		key->stencil_read_mask = 0xff;
		key->stencil_write_mask = random_below(256);
		key->blend = (unsigned char)random_below(2);
		key->blend_source = (unsigned char)random_below(15);
		key->depth_function = (unsigned char)random_below(8);
		key->cull = (unsigned char)random_below(2);
		key->samples = (unsigned char)random_below(5);
		key->views = (unsigned char)random_below(4);
		for (element = 0; element < D3DRS_PS_MAX; element++)
			key->pixel.combiner_state[element] = random_next();
		key->pixel.texture_modes = random_next();
		key->pixel.alpha_test_function = random_below(9);
		key->pixel.sampler_type[random_below(4)] = (unsigned char)random_below(4);
		key->pixel.multiview = (unsigned char)random_below(3);
		key->pixel.dynamic_lights = (unsigned char)random_below(3);
	}
	for (index = 0; index < LAYOUT_POOL; index++)
	{
		struct xgpu_vertex_input *input = &pool.layouts[index];

		memset(input, 0, sizeof(*input));
		input->declaration = 100 + index;
		input->element_count = 1 + random_below(5);
		for (element = 0; element < (int)input->element_count; element++)
		{
			input->elements[element].reg = (unsigned char)(element * 2 + random_below(2));
			input->elements[element].stream = (unsigned char)random_below(3);
			input->elements[element].format = (unsigned char)(1 + random_below(18));
			input->elements[element].bytes = (unsigned char)(4 * (1 + random_below(4)));
			input->elements[element].offset = (unsigned short)(element * 16);
		}
		input->packed_mask = random_below(2) ? 1u << input->elements[0].reg : 0;
	}
	for (index = 0; index < TEXTURE_POOL; index++)
	{
		pool.textures[index] = guest_allocate(sizeof(D3DBaseTexture));
		pool.textures[index]->Data = random_next();
		pool.textures[index]->Format = random_next();
		pool.textures[index]->Size = random_next();
	}
	random_bytes(&pool.uniforms, sizeof(pool.uniforms));
}

static void front_pipeline(void)
{
	const struct xgpu_pipeline_key *key = &pool.keys[random_below(KEY_POOL)];

	xgpu_backend_pipeline(key);
	front.key = *key;
	front.keyed = TRUE;
}

static void front_draw_state(void)
{
	struct xgpu_dynamic_state dynamic = front.dynamic;
	int index;

	if (!front.stated || random_below(3) == 0)
	{
		for (index = 0; index < 4; index++)
		{
			dynamic.viewport[index] = (long)random_below(2000) - 100;
			dynamic.scissor[index] = (long)random_below(2000);
			dynamic.blend_color[index] = (float)random_below(256) / 255.0f;
		}
		dynamic.depth_range[0] = 0;
		dynamic.depth_range[1] = (float)random_below(2);
		dynamic.stencil_reference = random_below(256);
		dynamic.depth_bias[0] = (float)random_below(5);
		dynamic.depth_bias[1] = -(float)random_below(5);
	}
	if (!front.stated || random_below(4) == 0)
	{
		pool.uniforms_serial++;
		pool.uniforms.ps_c0[random_below(8)][random_below(4)] = (float)random_next();
		pool.uniforms.viewport_scale[0] = (float)random_next();
	}
	xgpu_backend_draw_state(&dynamic, &pool.uniforms, pool.uniforms_serial);
	front.dynamic = dynamic;
	front.uniforms_serial = pool.uniforms_serial;
	{
		struct xgpu_draw_uniforms sent = pool.uniforms;

		finite_floats((float *)&sent, sizeof(sent) / sizeof(float));
		front.uniforms_checksum = checksum(&sent, sizeof(sent));
	}
	front.stated = TRUE;
}

static void front_textures(void)
{
	struct xgpu_texture_source stages[4];
	int stage;

	memcpy(stages, front.stages, sizeof(stages));
	for (stage = 0; stage < 4; stage++)
	{
		struct xgpu_texture_source *source = &stages[stage];
		unsigned long level;

		if (front.textured && random_below(3))
			continue;
		memset(source, 0, sizeof(*source));
		source->source = (unsigned char)random_below(3);
		source->sampler_type = (unsigned char)random_below(4);
		source->sampled = (unsigned char)random_below(2);
		if (source->source == XGPU_SOURCE_TEXTURE)
		{
			D3DBaseTexture *texture = pool.textures[random_below(TEXTURE_POOL)];

			source->resource = (DWORD)(uintptr_t)texture;
			source->palette = random_below(2) ? (DWORD)(uintptr_t)guest_allocate(1024) : 0;
			source->data = texture->Data;
			source->width = 1 + random_below(1024);
			source->height = 1 + random_below(1024);
			source->levels = 1 + random_below(10);
		}
		else if (source->source == XGPU_SOURCE_TARGET)
		{
			source->target = 1 + random_below(20);
			source->drawn = random_below(3);
			source->levels = random_below(4);
			source->rendered_levels = source->levels ? 1 + random_below(source->levels) : 0;
			for (level = 0; level < source->rendered_levels; level++)
			{
				source->level_targets[level] = 30 + random_below(10);
				source->level_written[level] = random_next();
			}
		}
		source->sampler.minify = (unsigned char)random_below(4);
		source->sampler.mip = (unsigned char)random_below(3);
		source->sampler.magnify = (unsigned char)random_below(3);
		source->sampler.address[random_below(3)] = (unsigned char)random_below(4);
		source->sampler.lod_bias = (float)random_below(100) / 37.0f;
		source->sampler.anisotropy = random_below(17);
		source->sampler.border_color = random_next();
	}
	xgpu_backend_set_textures(stages);
	memcpy(front.stages, stages, sizeof(stages));
	for (stage = 0; stage < 4; stage++)
	{
		front.stage_format[stage] = front.stage_size[stage] = 0;
		if (stages[stage].source == XGPU_SOURCE_TEXTURE && stages[stage].resource)
		{
			const D3DBaseTexture *texture = (const D3DBaseTexture *)(uintptr_t)stages[stage].resource;

			front.stage_format[stage] = (uint32_t)texture->Format;
			front.stage_size[stage] = (uint32_t)texture->Size;
		}
	}
	front.textured = TRUE;
}

/* streamed data, rewritten by the game as soon as the draw is made (to
show the stream copied it) */
static unsigned char *scribble[16];
static uint32_t scribble_size[16];

static void front_vertices(uint32_t draw)
{
	struct xgpu_vertex_input input = pool.layouts[random_below(LAYOUT_POOL)];
	int s, reg;

	for (s = 0; s < 3; s++)
	{
		input.streams[s].present = TRUE;
		input.streams[s].stride = 16 + 4 * random_below(8);
		if (random_below(2))
		{
			uint32_t bytes = pool.large_every && draw % pool.large_every == 0 ? pool.large_bytes :
				4 * (1 + random_below(pool.stream_bytes / 4));
			unsigned char *data = guest_allocate(bytes);

			random_bytes(data, bytes);
			input.streams[s].segment = -1;
			input.streams[s].data = (unsigned long)(uintptr_t)data;
			input.streams[s].bytes = bytes;
			front.stream_checksum[s] = checksum(data, bytes);
			scribble[s] = data;
			scribble_size[s] = bytes;
		}
		else
		{
			input.streams[s].segment = (long)random_below(4);
			input.streams[s].offset = random_below(0x400000);
			input.streams[s].bytes = random_below(0x10000);
		}
	}
	for (reg = 0; reg < 16; reg++)
	{
		if (random_below(6) == 0)
			front.attributes[reg][random_below(4)] = (float)random_next();
	}
	memcpy(input.attributes, front.attributes, sizeof(input.attributes));
	xgpu_backend_set_vertices(&input);
	front.input = input;
	front.vertexed = TRUE;
}

static void front_scribble(void)
{
	int s;

	for (s = 0; s < 16; s++)
	{
		if (scribble[s])
			memset(scribble[s], 0xa5, scribble_size[s]);
		scribble[s] = NULL;
	}
}

/* a draw of each kind, as its state is */
static void front_draw(uint32_t draw)
{
	uint32_t kind = random_below(10);
	int primitive = (int)random_below(7);

	if (kind < 4)
	{
		unsigned long count = 1 + random_below(3000);

		xgpu_backend_draw(primitive, count);
		text_printf(&expected, "draw %d %lu", primitive, count);
		print_front_state(&expected);
		text_printf(&expected, "\n");
	}
	else if (kind < 6)
	{
		unsigned long count = 1 + random_below(pool.stream_bytes / 2);
		WORD *indices = guest_allocate(count * 2);

		random_bytes(indices, count * 2);
		xgpu_backend_draw_indices(primitive, indices, count);
		text_printf(&expected, "draw indices %d %lu %08x", primitive, count, checksum(indices, count * 2));
		print_front_state(&expected);
		text_printf(&expected, "\n");
		memset(indices, 0x5a, count * 2);
	}
	else if (kind < 9)
	{
		unsigned long count = 1 + random_below(pool.stream_bytes / 2);
		long segment = random_below(3) == 0 ? -1 : (long)random_below(4);
		unsigned long minimum = random_below(100), offset = segment < 0 ? 0 : random_below(0x400000);
		BOOL temporary = random_below(2);
		WORD *indices = guest_allocate(count * 2);

		random_bytes(indices, count * 2);
		xgpu_backend_draw_indexed(primitive, indices, count, minimum, segment, offset, temporary);
		text_printf(&expected, "draw indexed %d %lu %lu %ld %lu %u", primitive, count, minimum, segment, offset,
			temporary);
		if (segment < 0)
			text_printf(&expected, " %08x", checksum(indices, count * 2));
		print_front_state(&expected);
		text_printf(&expected, "\n");
		memset(indices, 0x5a, count * 2);
	}
	else
	{
		unsigned long count = 1 + random_below(pool.immediate_vertices), index_count = random_below(3) ? 0 : count * 3 / 2;
		unsigned long written = 1 | random_below(0x10000);
		float *vertices = guest_allocate(count * 16 * 16);
		WORD *indices = index_count ? guest_allocate(index_count * 2) : NULL;
		uint32_t hash = 2166136261u;
		unsigned long vertex;
		int reg;

		random_bytes(vertices, count * 16 * 16);
		if (indices)
			random_bytes(indices, index_count * 2);
		for (reg = 0; reg < 16; reg++)
		{
			if (random_below(8) == 0)
				front.attributes[reg][random_below(4)] = (float)random_next();
		}
		xgpu_backend_draw_immediate(primitive, vertices, count, written, (const float (*)[4])front.attributes, indices,
			index_count);
		finite_floats(vertices, count * 16 * 4);
		/* (the packed vertices' checksum, register by register) */
		for (vertex = 0; vertex < count; vertex++)
		{
			for (reg = 0; reg < 16; reg++)
			{
				if (written & (1u << reg))
				{
					const unsigned char *bytes = (const unsigned char *)(vertices + (vertex * 16 + reg) * 4);
					int byte;

					for (byte = 0; byte < 16; byte++)
						hash = (hash ^ bytes[byte]) * 16777619u;
				}
			}
		}
		text_printf(&expected, "immediate %d %lu %lx %08x %lu", primitive, count, written, hash, index_count);
		if (index_count)
			text_printf(&expected, " %08x", checksum(indices, index_count * 2));
		for (reg = 0; reg < 16; reg++)
		{
			if (!(written & (1u << reg)))
				text_printf(&expected, " a%d %08x", reg, checksum(front.attributes[reg], 16));
		}
		if (front.keyed)
		{
			const struct xgpu_pipeline_key *k = &front.key;
			struct text *t = &expected;

			PRINT_KEY(t, k, k->vertex_program);
		}
		text_printf(&expected, "\n");
		memset(vertices, 0x3c, count * 16 * 16);
	}
	(void)draw;
	front_scribble();
}

static void front_frame(uint32_t frame, uint32_t draws)
{
	static const float color[4] = {0.25f, 0.5f, 0.75f, 1};
	long rectangles[2][4] = {{0, 0, 640, 480}, {10, 20, 30, 40}};
	uint32_t draw;

	xgpu_backend_set_targets(1, -1, 2, 1, 0, 0);
	text_printf(&expected, "targets 1 -1 2 1 0 0\n");
	xgpu_backend_clear(XGPU_CLEAR_COLOR | XGPU_CLEAR_DEPTH, color, 1.0f, frame & 0xff, rectangles, 1 + (frame & 1));
	text_printf(&expected, "clear %x %x %x %u", XGPU_CLEAR_COLOR | XGPU_CLEAR_DEPTH, float_bits(color[1]),
		float_bits(1.0f), frame & 0xff);
	for (draw = 0; draw < 1 + (frame & 1); draw++)
		text_printf(&expected, " %ld,%ld,%ld,%ld", rectangles[draw][0], rectangles[draw][1], rectangles[draw][2],
			rectangles[draw][3]);
	text_printf(&expected, "\n");
	for (draw = 0; draw < draws; draw++)
	{
		uint32_t what = random_below(20);

		if (what == 0)
		{
			float values[8][4];
			unsigned long first = random_below(180), count = 1 + random_below(8);

			random_bytes(values, sizeof(values));
			xgpu_backend_set_constants(first, (const float (*)[4])values, count);
			finite_floats(&values[0][0], count * 4);
			text_printf(&expected, "constants %lu %lu %08x\n", first, count, checksum(values, count * 16));
		}
		else if (what == 1)
		{
			unsigned long size = 1 + random_below(pool.stream_bytes);
			unsigned char *data = guest_allocate(size);
			unsigned long segment = random_below(4), offset = random_below(0x400000);
			BOOL unused = random_below(2);

			random_bytes(data, size);
			xgpu_backend_mirror_write(segment, offset, size, data, unused);
			text_printf(&expected, "mirror %lu %lu %lu %u %08x\n", segment, offset, size, unused, checksum(data, size));
			memset(data, 0x77, size);
		}
		else if (what == 2)
		{
			unsigned long slot = random_below(XGPU_VISIBILITY_TEST_SLOTS);

			xgpu_backend_visibility_begin();
			xgpu_backend_visibility_end(slot, 1.5f, 2, 4, slot * 5 + 2);
			text_printf(&expected, "visibility begin\nvisibility end %lu %x 2 4 %lu\n", slot, float_bits(1.5f),
				slot * 5 + 2);
		}
		if (random_below(3) == 0 || !front.keyed)
			front_pipeline();
		if (random_below(2) == 0 || !front.stated)
			front_draw_state();
		if (random_below(4) == 0 || !front.textured)
			front_textures();
		if (random_below(2) == 0 || !front.vertexed)
			front_vertices(draw);
		front_draw(draw);
	}
	xgpu_backend_present(1);
	text_printf(&expected, "present 1\n");
	xgpu_backend_frame_end();
	text_printf(&expected, "frame end\n");
}

/* ---------- the tests */

static void stream_begin(unsigned long ring_size, unsigned long frames_in_flight, unsigned gpu_microseconds)
{
	memset(&front, 0, sizeof(front));
	text_clear(&expected);
	text_clear(&actual);
	pthread_mutex_lock(&backend_state.lock);
	backend_state.keyed = backend_state.stated = backend_state.textured = backend_state.vertexed = FALSE;
	backend_state.gpu_microseconds = gpu_microseconds;
	backend_state.check_failures = 0;
	backend_state.pending_count = 0;
	pthread_mutex_unlock(&backend_state.lock);
	xgpu_stream_encode_configure(ring_size, frames_in_flight);
	xgpu_backend_initialize();
	text_printf(&expected, "initialize\n");
}

static void stream_end(const char *what)
{
	xgpu_stream_encode_close();
	CHECK(texts_compare(what, &expected, &actual));
	CHECK(backend_state.check_failures == 0);
}

/* Bounded waits let a stalled render thread fail the test before the GPU
is released, instead of hanging the test process. */
static int wait_for_backend_counter(const uint64_t *counter, uint64_t target)
{
	struct timespec until;
	int reached;

	clock_gettime(CLOCK_REALTIME, &until);
	until.tv_sec += 2;
	pthread_mutex_lock(&backend_state.lock);
	while (*counter < target)
		if (pthread_cond_timedwait(&backend_state.changed, &backend_state.lock, &until))
			break;
	reached = *counter >= target;
	pthread_mutex_unlock(&backend_state.lock);
	return reached;
}

/* New records must be played while an earlier frame is still on the GPU.
Completion alone must then release the ring, without a new submission. */
static void test_gpu_overlap(void)
{
	const struct xgpu_stream_ring *ring;
	struct timespec until, now;
	uint64_t first;
	uint32_t released;

	stream_begin(64u << 10, 8, 0);
	ring = xgpu_stream_encode_ring();
	host_render_stream_wait(ring->committed);
	released = __atomic_load_n(&ring->released, __ATOMIC_ACQUIRE);
	pthread_mutex_lock(&backend_state.lock);
	backend_state.gpu_held = TRUE;
	first = backend_state.fences_made + 1;
	pthread_mutex_unlock(&backend_state.lock);

	xgpu_backend_frame_end();
	text_printf(&expected, "frame end\n");
	CHECK(wait_for_backend_counter(&backend_state.fence_checked, first));
	/* The render thread has reached an idle queue with an unfinished fence. */
	xgpu_backend_invalidate();
	text_printf(&expected, "invalidate\n");
	xgpu_backend_frame_end();
	text_printf(&expected, "frame end\n");
	CHECK(wait_for_backend_counter(&backend_state.fences_made, first + 1));
	CHECK(__atomic_load_n(&ring->frames_retired, __ATOMIC_ACQUIRE) == 0);
	CHECK(__atomic_load_n(&ring->released, __ATOMIC_ACQUIRE) == released);

	pthread_mutex_lock(&backend_state.lock);
	CHECK(backend_state.fences_done == first - 1);
	backend_state.gpu_held = FALSE;
	pthread_cond_broadcast(&backend_state.changed);
	pthread_mutex_unlock(&backend_state.lock);
	/* Do not wake the render thread from the guest: only GPU completion
	can retire these frames while the stream is idle. */
	clock_gettime(CLOCK_MONOTONIC, &until);
	until.tv_sec += 2;
	while (__atomic_load_n(&ring->frames_retired, __ATOMIC_ACQUIRE) < 2)
	{
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (now.tv_sec > until.tv_sec || (now.tv_sec == until.tv_sec && now.tv_nsec >= until.tv_nsec))
			break;
		usleep(1000);
	}
	CHECK(__atomic_load_n(&ring->frames_retired, __ATOMIC_ACQUIRE) == 2);
	CHECK(__atomic_load_n(&ring->released, __ATOMIC_ACQUIRE) == ring->committed);
	stream_end("GPU overlap and idle retirement");
}

/* The backend must own the inline picture before the synchronous call returns. */
static void test_loading_art_transfer(void)
{
	unsigned char *pixels = malloc(sizeof(loading_art_copy));
	unsigned index, calls;

	CHECK(pixels != NULL);
	if (!pixels)
		return;
	for (index = 0; index < sizeof(loading_art_copy); index++)
		pixels[index] = (unsigned char)(index * 37u + index / 251u);
	calls = loading_art_calls;
	stream_begin(8u << 20, 2, 0);
	xgpu_backend_loading_art(pixels);
	CHECK(loading_art_calls == calls + 1 && loading_art_valid);
	CHECK(!memcmp(pixels, loading_art_copy, sizeof(loading_art_copy)));
	memset(pixels, 0xA5, sizeof(loading_art_copy));
	for (index = 0; index < sizeof(loading_art_copy); index++)
		CHECK(loading_art_copy[index] == (unsigned char)(index * 37u + index / 251u));
	xgpu_backend_loading_art(NULL);
	CHECK(loading_art_calls == calls + 2 && !loading_art_valid);
	for (index = 0; index < sizeof(loading_art_copy); index++)
		CHECK(loading_art_copy[index] == 0);
	stream_end("loading art copied and cleared");
	free(pixels);
}

/* The producer can stop completely during loading. Presentation still
advances; readiness stops it before gameplay reuses the rendering targets. */
static void test_loading_lifecycle(void)
{
	unsigned before, attempt;

	memset(&loading_test, 0, sizeof(loading_test));
	stream_begin(64u << 10, 2, 0);
	xgpu_backend_loading(1);
	for (attempt = 0; attempt < 500 && __atomic_load_n(&loading_test.frames, __ATOMIC_ACQUIRE) < 3; attempt++)
		usleep(1000);
	CHECK(__atomic_load_n(&loading_test.frames, __ATOMIC_ACQUIRE) >= 3);
	xgpu_backend_loading(2);
	/* The game may spend a whole interval building its first picture.
	Readiness must stay in that picture's batch while loading continues. */
	CHECK(__atomic_load_n(&loading_test.ready, __ATOMIC_ACQUIRE) == 0);
	before = __atomic_load_n(&loading_test.frames, __ATOMIC_ACQUIRE);
	for (attempt = 0; attempt < 500 && __atomic_load_n(&loading_test.frames, __ATOMIC_ACQUIRE) <= before; attempt++)
		usleep(1000);
	CHECK(__atomic_load_n(&loading_test.frames, __ATOMIC_ACQUIRE) > before);
	xgpu_backend_frame_end();
	text_printf(&expected, "frame end\n");
	xgpu_backend_consume();
	text_printf(&expected, "flush\n");
	CHECK(__atomic_load_n(&loading_test.ready, __ATOMIC_ACQUIRE) == 1);
	before = __atomic_load_n(&loading_test.frames, __ATOMIC_ACQUIRE);
	usleep(10000);
	CHECK(__atomic_load_n(&loading_test.frames, __ATOMIC_ACQUIRE) == before);
	xgpu_backend_loading(0);
	CHECK(__atomic_load_n(&loading_test.ended, __ATOMIC_ACQUIRE) == 1);
	CHECK(__atomic_load_n(&loading_test.invalid, __ATOMIC_ACQUIRE) == 0);
	stream_end("loading presentation");

	memset(&loading_test, 0, sizeof(loading_test));
	stream_begin(64u << 10, 2, 0);
	xgpu_backend_loading(1);
	stream_end("loading closed before a game picture");
	CHECK(__atomic_load_n(&loading_test.ended, __ATOMIC_ACQUIRE) == 1);

	/* A swap longer than its requested period must not turn every queued
	record into another swap. Loading work still advances in batches. */
	memset(&loading_test, 0, sizeof(loading_test));
	loading_test.slow = 1;
	stream_begin(64u << 10, 2, 0);
	xgpu_backend_loading(1);
	before = __atomic_load_n(&loading_test.frames, __ATOMIC_ACQUIRE);
	for (attempt = 0; attempt < 256; attempt++)
	{
		xgpu_backend_invalidate();
		text_printf(&expected, "invalidate\n");
	}
	xgpu_backend_loading(2);
	CHECK(__atomic_load_n(&loading_test.frames, __ATOMIC_ACQUIRE) - before < 128);
	CHECK(__atomic_load_n(&loading_test.invalid, __ATOMIC_ACQUIRE) == 0);
	xgpu_backend_loading(0);
	stream_end("preparation advances with a slow loading presenter");
}

/* every kind of call, as the backend should be given it */
static void test_round_trip(void)
{
	const struct xgpu_stream_ring *ring;
	struct xgpu_target_description description = {640, 480, FALSE, 1, XGPU_FORMAT_BGRA8};
	struct xgpu_vertex_program program;
	struct xgpu_vr_info info;
	struct xgpu_vr_frame vr_frame;
	unsigned char *pixels;
	uint32_t index;
	int frame;

	random_state = 0x1234567;
	pool.stream_bytes = 2048;
	pool.immediate_vertices = 64;
	pool.large_every = 0;
	pool_make();
	stream_begin(4u << 20, 2, 200);
	CHECK(xgpu_backend_capabilities.largest_target == 16384);
	CHECK(xgpu_backend_capabilities.most_samples == 8);
	CHECK(xgpu_backend_capabilities.dynamic_lights);

	xgpu_backend_target_create(7, &description);
	text_printf(&expected, "target 7 640 480 0 1\n");
	memset(&program, 0, sizeof(program));
	program.instruction_count = 12;
	program.hash = 0x0123456789abcdefull;
	program.projects = 1;
	random_bytes(program.instructions, sizeof(program.instructions));
	xgpu_backend_vertex_program(42, &program);
	text_printf(&expected, "program 42 0123456789abcdef 12 1 %08x\n",
		checksum(program.instructions, 12 * sizeof(program.instructions[0])));

	CHECK(xgpu_backend_vr_supported());
	text_printf(&expected, "vr supported\n");
	memset(&info, 0, sizeof(info));
	info.eye_width = 2000;
	info.eye_height = 1800;
	info.scope_size = 512;
	xgpu_backend_vr_initialize(&info);
	text_printf(&expected, "vr initialize 2000 1800 512\n");
	CHECK(info.foveated == 1 && info.fovea_width == 1000 && info.fovea_height == 900);

	for (frame = 0; frame < 40; frame++)
	{
		if (frame == 5)
		{
			xgpu_backend_invalidate();
			text_printf(&expected, "invalidate\n");
		}
		if (frame == 6)
		{
			xgpu_backend_flush();
			text_printf(&expected, "flush\n");
		}
		if (frame == 7)
		{
			struct xgpu_dynamic_lights_block *block = calloc(1, sizeof(*block));
			unsigned char *cells = guest_allocate(32 * 32 * 16 * 16);
			struct xgpu_dynamic_lights_halos *halos = calloc(1, sizeof(*halos));

			random_bytes(block->grid, sizeof(block->grid));
			random_bytes(block->position, 5 * 16);
			random_bytes(cells, 32 * 32 * 16 * 16);
			CHECK(xgpu_backend_dynamic_lights_prepare());
			xgpu_backend_dynamic_lights_upload(block, cells);
			text_printf(&expected, "lights %08x %08x\n", checksum(block, sizeof(*block)),
				checksum(cells, 32 * 32 * 16 * 16));
			memset(cells, 0, 32 * 32 * 16 * 16);
			halos->count = 3;
			random_bytes(halos->halos, 6 * 16);
			random_bytes(halos->camera_right, 43 * 4);
			xgpu_backend_dynamic_lights_halos(1, 2, 2, halos);
			text_printf(&expected, "halos 1 2 2 3 %08x %08x\n", checksum(halos->halos, 3 * 32),
				checksum(halos->camera_right, 43 * 4));
			free(block);
			free(halos);
		}
		if (frame == 8)
		{
			xgpu_backend_vr_pass(2);
			text_printf(&expected, "vr pass 2\n");
			memset(&vr_frame, 0, sizeof(vr_frame));
			vr_frame.eyes_drawn = TRUE;
			vr_frame.eye_target = 9;
			vr_frame.hud = 11;
			xgpu_backend_vr_submit(&vr_frame, TRUE);
			text_printf(&expected, "vr submit 1 9 11 1\n");
		}
		front_frame((uint32_t)frame, 30 + random_below(60));
		ring = xgpu_stream_encode_ring();
		CHECK(ring->frames_submitted - __atomic_load_n(&ring->frames_retired, __ATOMIC_ACQUIRE) <= 2);
	}

	/* a target's pixels, and a frame or two on: the answers the host
	writes into the guest's memory */
	pixels = xgpu_backend_read_target(7, -1, FALSE);
	text_printf(&expected, "read 7 -1 0 %u\n", 640 * 480 * 4);
	CHECK(pixels != NULL);
	for (index = 0; pixels && index < 640 * 480 * 4; index += 997)
		CHECK(pixels[index] == (unsigned char)(index * 7 + 7));
	for (index = 0; index < XGPU_VISIBILITY_TEST_SLOTS; index++)
	{
		unsigned long test;
		UINT result = xgpu_backend_visibility_result(index, &test);

		/* (each count with the number of its test, or neither yet) */
		CHECK((result == 0 && test == 0) || (result == index * 3 + 1 && test == index * 5 + 2));
	}
	/* the screenshot key's: the target read tone mapped, handed to the
	host's writer with its folder, and the file's name back; a failure's
	reason; and no writer, no read */
	{
		char name[64];

		memset(&screenshot_test, 0, sizeof(screenshot_test));
		host_render_stream_set_screenshot_writer(test_screenshot_writer);
		CHECK(xgpu_backend_screenshot(7, "/data/screenshots", name, sizeof(name)));
		text_printf(&expected, "read 7 -1 1 %u\n", 640 * 480 * 4);
		CHECK(!strcmp(name, "2026-01-02_03.04.05.png"));
		CHECK(screenshot_test.calls == 1 && screenshot_test.width == 640 && screenshot_test.height == 480);
		CHECK(!strcmp(screenshot_test.directory, "/data/screenshots"));
		CHECK(screenshot_test.pixels_read);
		CHECK(!xgpu_backend_screenshot(7, "/no/such/folder", name, sizeof(name)));
		text_printf(&expected, "read 7 -1 1 %u\n", 640 * 480 * 4);
		CHECK(!strcmp(name, "the screenshots folder cannot be made"));
		CHECK(!xgpu_backend_screenshot(7, "", name, sizeof(name)) && !name[0]);
		CHECK(!xgpu_backend_screenshot(99, "/data/screenshots", name, sizeof(name)) && !name[0]);
		host_render_stream_set_screenshot_writer(NULL);
		CHECK(!xgpu_backend_screenshot(7, "/data/screenshots", name, sizeof(name)) && !name[0]);
		CHECK(screenshot_test.calls == 2);
	}
	xgpu_backend_target_delete(7);
	text_printf(&expected, "target delete 7\n");

	ring = xgpu_stream_encode_ring();
	printf("round trip: %u frames, the largest %u bytes, %u waits, %u ms waited, the last decoded in %u us\n",
		ring->frames_submitted, ring->frame_bytes_most, ring->waits, ring->wait_nanoseconds / 1000000,
		ring->decode_nanoseconds / 1000);
	stream_end("round trip");
}

/* a pipeline the backend cannot make: TRUE until the host has said so,
FALSE after */
static void test_pipeline_failure(void)
{
	int index, failing = -1;

	random_state = 0x7654321;
	pool.stream_bytes = 512;
	pool.immediate_vertices = 16;
	pool.large_every = 0;
	pool_make();
	backend_state.fails_vertex_program = 3;
	for (index = 0; index < KEY_POOL; index++)
	{
		if ((uint32_t)pool.keys[index].vertex_program % 7 == 3)
			failing = index;
	}
	if (failing < 0)
	{
		pool.keys[0].vertex_program = (pool.keys[0].vertex_program & ~0xffffffffull) | 3;
		failing = 0;
	}
	stream_begin(1u << 20, 2, 0);
	CHECK(xgpu_backend_pipeline(&pool.keys[failing]));
	front.key = pool.keys[failing];
	front.keyed = TRUE;
	front_draw_state();
	front_textures();
	front_vertices(0);
	xgpu_backend_draw(XGPU_TRIANGLES, 3);
	text_printf(&expected, "draw %d 3", XGPU_TRIANGLES);
	print_front_state(&expected);
	text_printf(&expected, "\n");
	front_scribble();
	/* (a synchronous record: everything before it played) */
	CHECK(xgpu_backend_vr_supported());
	text_printf(&expected, "vr supported\n");
	CHECK(!xgpu_backend_pipeline(&pool.keys[failing]));
	stream_end("pipeline failure");
	backend_state.fails_vertex_program = 100;
}

/* a small ring, a slow GPU: the game waits for room, and for frames to
retire, and never more than frames_in_flight are unfinished */
static void test_ring_full(unsigned long frames_in_flight)
{
	const struct xgpu_stream_ring *ring;
	uint32_t most = 0;
	int frame;
	char what[64];

	random_state = 0x55aa55aa + frames_in_flight;
	pool.stream_bytes = 4096;
	pool.immediate_vertices = 16;
	/* (and draws whose data is too large for the ring: named in place,
	synchronous) */
	pool.large_every = 37;
	pool.large_bytes = 20000;
	pool_make();
	stream_begin(64u << 10, frames_in_flight, 1500);
	ring = xgpu_stream_encode_ring();
	for (frame = 0; frame < 30; frame++)
	{
		uint32_t unretired;

		front_frame((uint32_t)frame, 40 + random_below(40));
		unretired = ring->frames_submitted - __atomic_load_n(&ring->frames_retired, __ATOMIC_ACQUIRE);
		CHECK(unretired <= frames_in_flight);
		if (unretired > most)
			most = unretired;
	}
	printf("ring full (%lu in flight): %u frames, the largest %u bytes in a ring of %u, %u waits (%u ms), "
		"at most %u unretired, %u vertices sent again\n", frames_in_flight, ring->frames_submitted,
		ring->frame_bytes_most, ring->size, ring->waits, ring->wait_nanoseconds / 1000000, most, ring->vertices_again);
	CHECK(ring->vertices_again > 0);
	CHECK(ring->frame_bytes_most > ring->size);
	CHECK(ring->waits > 0);
	snprintf(what, sizeof(what), "ring full, %lu in flight", frames_in_flight);
	stream_end(what);
}

/* frames in flight: a fast game and a GPU slower than it, the ring large
enough: the game is held at frames_in_flight unretired */
static void test_frames_in_flight(void)
{
	const struct xgpu_stream_ring *ring;
	uint32_t most = 0;
	int frame;

	random_state = 0xfeedface;
	pool.stream_bytes = 256;
	pool.immediate_vertices = 16;
	pool.large_every = 0;
	pool_make();
	stream_begin(8u << 20, 2, 20000);
	ring = xgpu_stream_encode_ring();
	for (frame = 0; frame < 12; frame++)
	{
		uint32_t unretired;

		front_frame((uint32_t)frame, 10);
		unretired = ring->frames_submitted - __atomic_load_n(&ring->frames_retired, __ATOMIC_ACQUIRE);
		CHECK(unretired <= 2);
		if (unretired > most)
			most = unretired;
	}
	printf("frames in flight: at most %u unretired after a frame's submit, %u waits\n", most, ring->waits);
	/* (the game got ahead of the GPU, and was held) */
	CHECK(most == 2);
	CHECK(ring->waits >= 8);
	stream_end("frames in flight");
}

/* display.low_latency's pacing (host_render_stream_frame_displayed): a
display that takes each frame as it ends paces the game, which never has
more than FRAME_PACING_QUEUED of its frames untaken (nor waits the longest,
100 ms, for one), and starts each in time for the refresh it is for. A
render thread kept off the processor misses refreshes whatever the pacing
does (7 of 30 on a machine busy with builds), so a run with misses is tried
again: the pacing's own fault misses in every one */
static void test_display_paced(void)
{
	struct host_render_pacing_statistics statistics;
	const struct xgpu_stream_ring *ring;
	uint64_t started, elapsed;
	int attempt, frame;

	for (attempt = 0; attempt < 3; attempt++)
	{
		random_state = 0xdecade;
		pool.stream_bytes = 256;
		pool.immediate_vertices = 16;
		pool.large_every = 0;
		pool_make();
		stream_begin(8u << 20, 2, 0);
		ring = xgpu_stream_encode_ring();
		host_render_stream_pacing_statistics(&statistics);
		display_period = 4000000u;
		host_render_stream_frames_taken_reset();
		host_render_stream_display_paced(1);
		started = test_now();
		for (frame = 0; frame < 30; frame++)
			front_frame((uint32_t)frame, 4);
		elapsed = test_now() - started;
		host_render_stream_pacing_statistics(&statistics);
		host_render_stream_display_paced(0);
		display_period = 0;
		printf("display paced: 30 frames in %.1f ms, %u at their refresh, %u after it, %u started late, budget %.2f ms\n",
			(double)elapsed / 1e6, statistics.kept, statistics.missed, statistics.late_starts,
			(double)statistics.budget / 1e6);
		/* (no wait for a display that took nothing: 100 ms each) */
		CHECK(elapsed < 1000000000u);
		CHECK(ring->frames_submitted >= 30);
		stream_end("display paced");
		if (statistics.missed == 0)
			break;
	}
	CHECK(statistics.missed == 0);
	CHECK(statistics.kept >= 20);
}

/* a ring and more of other records between a set_vertices and its draw:
the vertices' data, which the ring's bytes no longer hold, is sent again
as it was */
static void test_vertices_kept(void)
{
	const struct xgpu_stream_ring *ring;
	int round, write;

	random_state = 0x13579bdf;
	pool.stream_bytes = 2048;
	pool.immediate_vertices = 16;
	pool.large_every = 0;
	pool_make();
	stream_begin(64u << 10, 2, 300);
	front_pipeline();
	front_draw_state();
	front_textures();
	for (round = 0; round < 6; round++)
	{
		front_vertices(1);
		for (write = 0; write < 50; write++)
		{
			unsigned long size = 4000;
			unsigned char *data = guest_allocate(size);

			random_bytes(data, size);
			xgpu_backend_mirror_write(1, (unsigned long)write * size, size, data, FALSE);
			text_printf(&expected, "mirror 1 %lu %lu 0 %08x\n", (unsigned long)write * size, size, checksum(data, size));
		}
		/* (the game's copy changes too: the stream's must not) */
		front_draw(1);
		xgpu_backend_draw(XGPU_TRIANGLES, 3);
		text_printf(&expected, "draw %d 3", XGPU_TRIANGLES);
		print_front_state(&expected);
		text_printf(&expected, "\n");
		xgpu_backend_present(1);
		text_printf(&expected, "present 1\n");
		xgpu_backend_frame_end();
		text_printf(&expected, "frame end\n");
	}
	ring = xgpu_stream_encode_ring();
	printf("vertices kept: %u vertices sent again, %u waits\n", ring->vertices_again, ring->waits);
	CHECK(ring->vertices_again >= 6);
	stream_end("vertices kept");
}

/* malformed records stop the decoder where they are, and say why */
static void test_malformed(void)
{
	struct xgpu_stream_ring *ring = guest_allocate(sizeof(*ring));
	uint32_t *buffer = xgpu_stream_test_allocate(XGPU_STREAM_RING_ALIGNMENT, 0x10000);
	unsigned char *pipelines = guest_allocate(XGPU_STREAM_PIPELINES);
	struct host_render_decoder *decoder;

	ring->buffer = guest_address(buffer);
	ring->size = 0x10000;
	ring->pipelines = guest_address(pipelines);
	decoder = host_render_decoder_new(&test_backend, ring);

	buffer[0] = XGPU_STREAM_HEADER(XGPU_STREAM_FLUSH, 4);
	buffer[1] = XGPU_STREAM_HEADER(200, 8);
	text_clear(&actual);
	CHECK(host_render_decode(decoder, 0, 12) == 4);
	CHECK(ring->error == XGPU_STREAM_ERROR_RECORD && ring->error_position == 4);

	ring->error = 0;
	buffer[1] = XGPU_STREAM_HEADER(XGPU_STREAM_DRAW, 64);
	CHECK(host_render_decode(decoder, 0, 12) == 4);
	CHECK(ring->error == XGPU_STREAM_ERROR_BOUNDS);

	ring->error = 0;
	buffer[1] = XGPU_STREAM_HEADER(XGPU_STREAM_PIPELINE, 8);
	buffer[2] = 9;
	CHECK(host_render_decode(decoder, 0, 12) == 4);
	CHECK(ring->error == XGPU_STREAM_ERROR_ID);

	ring->error = 0;
	buffer[1] = XGPU_STREAM_HEADER(XGPU_STREAM_DRAW, 12);
	buffer[2] = XGPU_TRIANGLES;
	buffer[3] = 3;
	/* (inline data outside its record) */
	buffer[4] = XGPU_STREAM_HEADER(XGPU_STREAM_DRAW_INDICES, 16);
	buffer[5] = XGPU_TRIANGLES;
	buffer[6] = 3;
	buffer[7] = guest_address(buffer) + 64;
	CHECK(host_render_decode(decoder, 0, 32) == 16);
	CHECK(ring->error == XGPU_STREAM_ERROR_BOUNDS);

	/* Reject oversized/legacy counts, missing pixels and extra clear bytes.
	No malformed record may reach the backend or clear its picture. */
	{
		unsigned calls = loading_art_calls;
		ring->error = 0;
		buffer[0] = XGPU_STREAM_HEADER(XGPU_STREAM_LOADING_ART, 8);
		buffer[1] = XGPU_STREAM_LOADING_ART_BYTES + 4;
		CHECK(host_render_decode(decoder, 0, 8) == 0);
		CHECK(ring->error == XGPU_STREAM_ERROR_RECORD);
		ring->error = 0;
		buffer[1] = 320u * 240u;
		CHECK(host_render_decode(decoder, 0, 8) == 0);
		CHECK(ring->error == XGPU_STREAM_ERROR_RECORD);
		ring->error = 0;
		buffer[1] = XGPU_STREAM_LOADING_ART_BYTES;
		CHECK(host_render_decode(decoder, 0, 8) == 0);
		CHECK(ring->error == XGPU_STREAM_ERROR_RECORD);
		ring->error = 0;
		buffer[0] = XGPU_STREAM_HEADER(XGPU_STREAM_LOADING_ART, 12);
		buffer[1] = 4;
		buffer[2] = 0;
		CHECK(host_render_decode(decoder, 0, 12) == 0);
		CHECK(ring->error == XGPU_STREAM_ERROR_RECORD);
		ring->error = 0;
		buffer[1] = 0;
		buffer[2] = 0;
		CHECK(host_render_decode(decoder, 0, 12) == 0);
		CHECK(ring->error == XGPU_STREAM_ERROR_RECORD);
		CHECK(loading_art_calls == calls);
	}

	/* a skip to the ring's end, and on from its start */
	ring->error = 0;
	buffer[0x10000 / 4 - 2] = XGPU_STREAM_HEADER(XGPU_STREAM_SKIP, 0);
	buffer[0] = XGPU_STREAM_HEADER(XGPU_STREAM_FLUSH, 4);
	CHECK(host_render_decode(decoder, 0x10000 - 8, 0x10000 + 4) == 0x10000 + 4);
	CHECK(ring->error == 0);
	host_render_decoder_free(decoder);
}

int main(void)
{
	struct host_render_backend asynchronous_backend = test_backend;

	window_make();
	host_render_stream_set_backend(&test_backend);
	pthread_create(&backend_state.thread, NULL, gpu_thread, NULL);
	backend_state.fails_vertex_program = 100;

	test_malformed();
	test_round_trip();
	test_vertices_kept();
	test_pipeline_failure();
	test_ring_full(2);
	test_ring_full(1);
	test_frames_in_flight();
	test_loading_art_transfer();
	test_loading_lifecycle();
	test_display_paced();

	asynchronous_backend.fence_ready = test_fence_ready;
	host_render_stream_set_backend(&asynchronous_backend);
	test_gpu_overlap();
	test_ring_full(2);
	test_ring_full(1);
	test_frames_in_flight();
	test_loading_lifecycle();

	pthread_mutex_lock(&backend_state.lock);
	backend_state.stopping = TRUE;
	pthread_cond_broadcast(&backend_state.changed);
	pthread_mutex_unlock(&backend_state.lock);
	pthread_join(backend_state.thread, NULL);
	if (failures)
	{
		fprintf(stderr, "%d failed\n", failures);
		return 1;
	}
	printf("all passed\n");
	return 0;
}
