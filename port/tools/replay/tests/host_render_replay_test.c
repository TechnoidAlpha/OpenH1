/*
HOST_RENDER_REPLAY_TEST.C

The replays' shared part (port/tools/replay/host_render_replay.c) tested
without a GPU or the game (tools/test_render_replay.py): a backend of its
own that draws rectangles on the CPU, a recording made here of a few frames
of them, and the replay's service driven over its pipes by the test.

host_render_replay_test make <file> <frames>  the recording: each frame a
	clear of target 1 (64x32) to (f, 0x20, 0x40), a red rectangle at x f % 40,
	a green one that stays, and a third in the clear's own colour (which
	changes no pixel), then the present
host_render_replay_test unit                  the LZ4 decoder, base64, the
	PNG writer (its file at argv[2] for the test to read), the requests'
	parser
host_render_replay_test <file> --serve        the replay, with this backend

The backend's GPU time (cost) is made up: a clear takes COST_CLEAR ms, a
draw COST_PIXEL ms a pixel of its rectangle (whether it changed them or
not), the frame's present a pass of its own of COST_PRESENT; the draws are
timed one by one as well (the "timestamps" method).
*/

#include "../host_render_replay.c"

#ifdef __APPLE__
#include <compression.h>
#endif

#define TARGET_WIDTH 64
#define TARGET_HEIGHT 32

#define COST_CLEAR 0.05
#define COST_PIXEL 0.001
#define COST_PRESENT 0.01

/* ---------- the backend */

static struct
{
	uint32_t width, height;
	unsigned char *pixels;
	uint32_t color;
	uint64_t fence;
	/* the ID buffer's (the target looked at, armed: a draw's number) */
	uint32_t ids_target, ids_number, ids_depth;
	int ids_armed;
	uint32_t *ids;
	/* the frame timed: its draws' made-up times, the draw being handed */
	int timing;
	uint32_t timing_draw;
	double clear_ms, draw_ms[16];
	uint32_t draw_pass[16], draw_count;
} fake;

static void fake_open(void *context, void *ring, uint32_t ring_size, volatile uint32_t *visibility)
{
	(void)context;
	(void)ring;
	(void)ring_size;
	(void)visibility;
}

static uint64_t fake_fence(void *context)
{
	(void)context;
	return ++fake.fence;
}

static void fake_wait(void *context, uint64_t fence)
{
	(void)context;
	(void)fence;
}

static int fake_fence_ready(void *context, uint64_t fence)
{
	(void)context;
	(void)fence;
	return 1;
}

static void fake_target_create(void *context, const struct xgpu_stream_target_create *record)
{
	(void)context;
	if (record->target != 1)
		return;
	free(fake.pixels);
	fake.width = record->width;
	fake.height = record->height;
	fake.pixels = calloc((size_t)fake.width * fake.height, 4);
}

static void fake_set_targets(void *context, const struct xgpu_stream_set_targets *record)
{
	(void)context;
	fake.color = record->color;
}

static void fake_clear_target(void *context, const struct xgpu_stream_clear_target *record)
{
	size_t pixel;

	(void)context;
	if (fake.timing)
		fake.clear_ms += COST_CLEAR;
	if (record->target != 1 || !fake.pixels)
		return;
	for (pixel = 0; pixel < (size_t)fake.width * fake.height; pixel++)
	{
		fake.pixels[pixel * 4] = (unsigned char)(record->color[2] * 255.0f + 0.5f);
		fake.pixels[pixel * 4 + 1] = (unsigned char)(record->color[1] * 255.0f + 0.5f);
		fake.pixels[pixel * 4 + 2] = (unsigned char)(record->color[0] * 255.0f + 0.5f);
		fake.pixels[pixel * 4 + 3] = 0xff;
	}
}

/* a rectangle (count: x, y, width, height, a byte each) of a colour
(primitive: BGRA's bytes) */
static void fake_draw(void *context, const struct xgpu_stream_draw *record)
{
	uint32_t x0 = record->count & 0xff, y0 = (record->count >> 8) & 0xff, width = (record->count >> 16) & 0xff;
	uint32_t height = record->count >> 24, x, y;
	unsigned char color[4];

	(void)context;
	memcpy(color, &record->primitive, 4);
	if (fake.timing && fake.timing_draw < 16)
	{
		fake.draw_ms[fake.timing_draw] = width * height * COST_PIXEL;
		fake.draw_pass[fake.timing_draw] = 0;
		if (fake.timing_draw + 1 > fake.draw_count)
			fake.draw_count = fake.timing_draw + 1;
	}
	if (fake.color != 1 || !fake.pixels)
		return;
	for (y = y0; y < y0 + height && y < fake.height; y++)
	{
		for (x = x0; x < x0 + width && x < fake.width; x++)
		{
			unsigned char *pixel = fake.pixels + ((size_t)y * fake.width + x) * 4;

			if (fake.ids && fake.ids_armed && fake.ids_target == 1 && memcmp(pixel, color, 4))
			{
				uint32_t *stack = fake.ids + ((size_t)y * fake.width + x) * fake.ids_depth, layer;

				if (stack[0] != fake.ids_number + 1)
				{
					for (layer = fake.ids_depth - 1; layer > 0; layer--)
						stack[layer] = stack[layer - 1];
					stack[0] = fake.ids_number + 1;
				}
			}
			memcpy(pixel, color, 4);
		}
	}
}

static void fake_present(void *context, uint32_t back_buffer)
{
	(void)context;
	(void)back_buffer;
}

static void fake_frame_end(void *context)
{
	(void)context;
}

static const struct host_render_backend fake_backend =
{
	.open = fake_open,
	.fence = fake_fence,
	.wait = fake_wait,
	.fence_ready = fake_fence_ready,
	.target_create = fake_target_create,
	.set_targets = fake_set_targets,
	.clear_target = fake_clear_target,
	.draw = fake_draw,
	.present = fake_present,
	.frame_end = fake_frame_end,
};

/* ---------- the platform */

static int fake_initialize(int window, int width, int height)
{
	(void)window;
	(void)width;
	(void)height;
	host_render_stream_set_backend(&fake_backend);
	return 1;
}

static unsigned char *fake_read_target(uint32_t target, uint32_t *width, uint32_t *height)
{
	unsigned char *pixels;

	if (target != 1 || !fake.pixels)
		return NULL;
	*width = fake.width;
	*height = fake.height;
	pixels = malloc((size_t)fake.width * fake.height * 4);
	if (pixels)
		memcpy(pixels, fake.pixels, (size_t)fake.width * fake.height * 4);
	return pixels;
}

static int fake_ids_begin(uint32_t target, uint32_t depth)
{
	free(fake.ids);
	fake.ids_target = target;
	fake.ids_depth = depth;
	fake.ids_armed = 0;
	fake.ids = calloc((size_t)TARGET_WIDTH * TARGET_HEIGHT * depth, sizeof(*fake.ids));
	return fake.ids != NULL;
}

static void fake_ids_draw(uint32_t index)
{
	fake.ids_armed = index != UINT32_MAX;
	fake.ids_number = index;
}

static uint32_t *fake_ids_read(uint32_t target, uint32_t *width, uint32_t *height)
{
	uint32_t *ids;

	if (target != fake.ids_target || !fake.ids)
		return NULL;
	*width = TARGET_WIDTH;
	*height = TARGET_HEIGHT;
	ids = malloc((size_t)TARGET_WIDTH * TARGET_HEIGHT * fake.ids_depth * sizeof(*ids));
	if (ids)
		memcpy(ids, fake.ids, (size_t)TARGET_WIDTH * TARGET_HEIGHT * fake.ids_depth * sizeof(*ids));
	return ids;
}

static void fake_ids_end(void)
{
	free(fake.ids);
	fake.ids = NULL;
}

static int fake_cost_begin(void)
{
	memset(fake.draw_ms, 0, sizeof(fake.draw_ms));
	fake.clear_ms = 0.0;
	fake.draw_count = 0;
	fake.timing_draw = UINT32_MAX;
	fake.timing = 1;
	return 1;
}

static void fake_cost_draw(uint32_t index)
{
	fake.timing_draw = index;
}

static int fake_cost_end(struct host_render_replay_cost *cost)
{
	static struct host_render_replay_pass_time passes[2];
	uint32_t draw;

	fake.timing = 0;
	memset(passes, 0, sizeof(passes));
	passes[0].kind = "test";
	passes[0].width = TARGET_WIDTH;
	passes[0].height = TARGET_HEIGHT;
	passes[0].ms = fake.clear_ms;
	for (draw = 0; draw < fake.draw_count; draw++)
	{
		passes[0].ms += fake.draw_ms[draw];
		passes[0].draws++;
	}
	passes[1].kind = "present";
	passes[1].ms = COST_PRESENT;
	passes[1].start_ms = passes[0].ms;
	cost->passes = passes;
	cost->pass_count = 2;
	cost->frame_ms = passes[0].ms + passes[1].ms;
	cost->draw_pass = fake.draw_pass;
	cost->draw_count = fake.draw_count;
	cost->draw_ms = fake.draw_ms;
	cost->draw_method = "timestamps";
	cost->note = "made up";
	return 1;
}

static const struct host_render_replay_platform fake_platform =
{
	.backend = "test",
	.initialize = fake_initialize,
	.read_target = fake_read_target,
	.ids_begin = fake_ids_begin,
	.ids_draw = fake_ids_draw,
	.ids_read = fake_ids_read,
	.ids_end = fake_ids_end,
	.cost_begin = fake_cost_begin,
	.cost_draw_method = "timestamps",
	.cost_draw = fake_cost_draw,
	.cost_end = fake_cost_end,
};

/* ---------- the recording made */

#define RING_ADDRESS 0x100000u
#define RING_BUFFER 0x200000u
#define RING_SIZE 0x100000u

static unsigned char *events;
static size_t events_size, events_capacity;
static uint32_t position;

static void events_add(const void *data, size_t size)
{
	if (events_size + size > events_capacity)
	{
		events_capacity = (events_size + size) * 2;
		events = realloc(events, events_capacity);
	}
	memcpy(events + events_size, data, size);
	events_size += size;
}

/* a record of type at the next position (its bytes after the header) */
static void record_add(uint32_t type, void *record, uint32_t size)
{
	struct host_render_record_event event;
	uint32_t header = XGPU_STREAM_HEADER(type, size);

	memcpy(record, &header, 4);
	event.type = HOST_RENDER_EVENT_RECORD;
	event.size = 4 + size;
	events_add(&event, sizeof(event));
	events_add(&position, 4);
	events_add(record, size);
	position += size;
}

static uint32_t rectangle(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	return x | y << 8 | width << 16 | height << 24;
}

/* debug.render_marks' records (xgpu_stream.h): a string (its bytes inline,
at the record's 16-byte aligned place), a view, an owner */
static void mark_string_add(uint32_t number, const char *text)
{
	unsigned char bytes[256];
	struct xgpu_stream_mark_string string;
	uint32_t length = (uint32_t)strlen(text), offset = 16, size = (offset + length + 1 + 3) & ~3u;

	memset(bytes, 0, sizeof(bytes));
	string.header = 0;
	string.number = number;
	string.length = length;
	string.data = RING_BUFFER + (position & (RING_SIZE - 1)) + offset;
	memcpy(bytes, &string, sizeof(string));
	memcpy(bytes + offset, text, length);
	record_add(XGPU_STREAM_MARK_STRING, bytes, size);
}

static void mark_owner_add(uint32_t kind, uint32_t id, uint32_t label, uint32_t object, uint32_t shader,
	uint32_t widget, uint32_t source)
{
	struct xgpu_stream_mark_owner owner;

	memset(&owner, 0, sizeof(owner));
	owner.kind = kind;
	owner.id = id;
	owner.label = label;
	owner.object = object;
	owner.shader = shader;
	owner.widget = widget;
	owner.source = source;
	record_add(XGPU_STREAM_MARK_OWNER, &owner, sizeof(owner));
}

/* a recording of frames of rectangles: with marks (the first draw a
world object's, the second a menu widget's, the third of no owner's: the
owner before it ends at the view), or as a recording of version 1 (the
stream's 21, before the marks) */
static int make(const char *path, uint32_t frames, int with_marks, int old)
{
	struct host_render_record_file file;
	struct host_render_record_block block;
	uint32_t frame;
	FILE *out;

	memset(&file, 0, sizeof(file));
	file.magic = HOST_RENDER_RECORD_MAGIC;
	file.version = old ? 1 : HOST_RENDER_RECORD_VERSION;
	file.header_size = sizeof(file);
	file.page_size = (uint32_t)sysconf(_SC_PAGESIZE);
	file.block_size = HOST_RENDER_RECORD_BLOCK;
	file.ring_address = RING_ADDRESS;
	file.ring.magic = XGPU_STREAM_MAGIC;
	file.ring.version = old ? XGPU_STREAM_VERSION - (HOST_RENDER_RECORD_VERSION - 1) : XGPU_STREAM_VERSION;
	file.ring.header_size = sizeof(file.ring);
	file.ring.buffer = RING_BUFFER;
	file.ring.size = RING_SIZE;
	file.ring.frames_in_flight = 2;
	file.ring.visibility = 0x400000;
	file.ring.pipelines = 0x500000;
	for (frame = 0; frame < frames; frame++)
	{
		struct xgpu_stream_target_create create;
		struct xgpu_stream_set_targets targets;
		struct xgpu_stream_clear_target clear;
		struct xgpu_stream_draw draw;
		struct xgpu_stream_value present;
		struct xgpu_stream_frame_end end;

		if (!frame)
		{
			memset(&create, 0, sizeof(create));
			create.target = 1;
			create.width = TARGET_WIDTH;
			create.height = TARGET_HEIGHT;
			create.layers = 1;
			record_add(XGPU_STREAM_TARGET_CREATE, &create, sizeof(create));
		}
		memset(&targets, 0, sizeof(targets));
		targets.color = 1;
		targets.views = 1;
		record_add(XGPU_STREAM_SET_TARGETS, &targets, sizeof(targets));
		memset(&clear, 0, sizeof(clear));
		clear.target = 1;
		clear.color[0] = (float)frame / 255.0f;
		clear.color[1] = 0x20 / 255.0f;
		clear.color[2] = 0x40 / 255.0f;
		clear.color[3] = 1.0f;
		record_add(XGPU_STREAM_CLEAR_TARGET, &clear, sizeof(clear));
		if (with_marks)
		{
			struct xgpu_stream_mark_view view;

			if (!frame)
			{
				mark_string_add(1, "levels\\test\\test");
				mark_string_add(2, "warthog #2");
				mark_string_add(3, "vehicles\\warthog\\warthog");
				mark_string_add(4, "vehicles\\warthog\\shaders\\warthog hull");
				mark_string_add(5, "pc\\main_menu\\board_start");
				mark_string_add(6, "tools/port_settings.py multiplayer_files/_lobby/_lobby_board");
				/* (a label cut in a character: not UTF-8) */
				mark_string_add(7, "caf\xc3");
			}
			memset(&view, 0, sizeof(view));
			view.map = 1;
			view.tick = 1000 + frame;
			view.window = 0;
			view.position[0] = (float)frame;
			view.forward[1] = 1.0f;
			view.up[2] = 1.0f;
			view.field_of_view = 1.5f;
			view.viewport[2] = TARGET_WIDTH;
			view.viewport[3] = TARGET_HEIGHT;
			record_add(XGPU_STREAM_MARK_VIEW, &view, sizeof(view));
			mark_owner_add(XGPU_STREAM_MARK_WORLD_OBJECT, 0x10002, 2, 3, 4, 0, 0);
		}
		memset(&draw, 0, sizeof(draw));
		draw.primitive = 0xff0000ffu;
		draw.count = rectangle(frame % 40, 4, 16, 8);
		record_add(XGPU_STREAM_DRAW, &draw, sizeof(draw));
		if (with_marks)
			mark_owner_add(XGPU_STREAM_MARK_MENU_WIDGET, 7, 7, 0, 0, 5, 6);
		draw.primitive = 0xff00ff00u;
		draw.count = rectangle(20, 8, 20, 16);
		record_add(XGPU_STREAM_DRAW, &draw, sizeof(draw));
		if (with_marks)
			mark_owner_add(XGPU_STREAM_MARK_OTHER, XGPU_STREAM_MARK_NONE, 0, 0, 0, 0, 0);
		draw.primitive = 0xff000000u | frame << 16 | 0x20 << 8 | 0x40;
		draw.count = rectangle(0, 26, 64, 6);
		record_add(XGPU_STREAM_DRAW, &draw, sizeof(draw));
		memset(&present, 0, sizeof(present));
		present.value = 1;
		record_add(XGPU_STREAM_PRESENT, &present, sizeof(present));
		memset(&end, 0, sizeof(end));
		record_add(XGPU_STREAM_FRAME_END, &end, sizeof(end));
	}
	out = fopen(path, "wb");
	if (!out)
		return 2;
	block.size = block.stored = (uint32_t)events_size;
	fwrite(&file, sizeof(file), 1, out);
	fwrite(&block, sizeof(block), 1, out);
	fwrite(events, 1, events_size, out);
	return fclose(out) ? 2 : 0;
}

/* ---------- the units */

static int failures;

#define CHECK(condition) do { if (!(condition)) { failures++; \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static void lz4_test(void)
{
	/* a stored block, and an LZ4 block of a literal and an overlapping
	match (its offset 1: "a" made "aaaaaaaa"), then the end */
	static const unsigned char framed[] =
	{
		'b', 'v', '4', '-', 3, 0, 0, 0, 'x', 'y', 'z',
		'b', 'v', '4', '1', 9, 0, 0, 0, 6, 0, 0, 0, 0x13, 'a', 1, 0, 0x10, 'b',
		'b', 'v', '4', '$',
	};
	unsigned char decoded[64];

	CHECK(host_render_replay_lz4_decode(decoded, sizeof(decoded), framed, sizeof(framed)) == 12);
	CHECK(!memcmp(decoded, "xyzaaaaaaaab", 12));
	/* (too small a buffer, and a match before the start) */
	CHECK(host_render_replay_lz4_decode(decoded, 5, framed, sizeof(framed)) == 0);
	{
		static const unsigned char bad[] = { 'b', 'v', '4', '1', 5, 0, 0, 0, 3, 0, 0, 0, 0x00, 4, 0 };

		CHECK(host_render_replay_lz4_decode(decoded, sizeof(decoded), bad, sizeof(bad)) == 0);
	}
#ifdef __APPLE__
	{
		/* Apple's own encoder's frames, of text, of noise and of runs */
		size_t size = 3 << 20, index, encoded_size;
		unsigned char *input = malloc(size), *encoded = malloc(size + 4096), *output = malloc(size);
		void *scratch = malloc(compression_encode_scratch_buffer_size(COMPRESSION_LZ4));
		uint32_t state = 12345;

		for (index = 0; index < size; index++)
		{
			state = state * 1103515245u + 12345u;
			input[index] = index < size / 3 ? (unsigned char)("the replay's recording "[index % 23]) :
				index < size / 3 * 2 ? (unsigned char)(state >> 16) : (unsigned char)(index / 1000);
		}
		encoded_size = compression_encode_buffer(encoded, size + 4096, input, size, scratch, COMPRESSION_LZ4);
		CHECK(encoded_size > 0);
		CHECK(host_render_replay_lz4_decode(output, size, encoded, encoded_size) == size);
		CHECK(!memcmp(input, output, size));
		free(input);
		free(encoded);
		free(output);
		free(scratch);
	}
#endif
}

static void base64_test(void)
{
	static const struct
	{
		const char *in, *out;
	} cases[] = { { "", "" }, { "f", "Zg==" }, { "fo", "Zm8=" }, { "foo", "Zm9v" }, { "foobar", "Zm9vYmFy" } };
	size_t index;

	for (index = 0; index < sizeof(cases) / sizeof(*cases); index++)
	{
		char *encoded = host_render_replay_base64((const unsigned char *)cases[index].in, strlen(cases[index].in));

		CHECK(encoded && !strcmp(encoded, cases[index].out));
		free(encoded);
	}
}

/* deflate_bytes' streams written for the test to inflate (deflate_<n>.bin:
text, noise, runs, a large frame's worth of mixed bytes, and nothing) */
static void deflate_test(const char *folder)
{
	size_t sizes[5] = { 5000, 3000, 70000, 3 << 20, 0 }, index, at, compressed;
	uint32_t state = 7;

	for (index = 0; index < 5; index++)
	{
		unsigned char *data = malloc(sizes[index] + 1), *zipped;
		char path[4200];

		for (at = 0; at < sizes[index]; at++)
		{
			state = state * 1103515245u + 12345u;
			data[at] = index == 0 ? (unsigned char)("the replay's deflate "[at % 21]) : index == 1 ?
				(unsigned char)(state >> 16) : index == 2 ? (unsigned char)(at / 3000) :
				at % 7 < 3 ? (unsigned char)(state >> 20) : (unsigned char)(at / 640);
		}
		zipped = deflate_bytes(data, sizes[index], &compressed);
		CHECK(zipped != NULL);
		snprintf(path, sizeof(path), "%s/deflate_%zu.bin", folder, index);
		CHECK(zipped && file_write(path, zipped, compressed));
		snprintf(path, sizeof(path), "%s/deflate_%zu.raw", folder, index);
		CHECK(file_write(path, data, sizes[index]));
		free(zipped);
		free(data);
	}
}

static void request_test(void)
{
	static struct request request;
	double number;

	CHECK(request_parse("{\"cmd\":\"frame\",\"n\":12,\"inline\":true,\"png\":\"/a \\\"b\\\"\",\"frames\":[1, 2,3]}",
		&request));
	CHECK(!strcmp(request_string(&request, "cmd"), "frame"));
	CHECK(request_number(&request, "n", &number) && number == 12);
	CHECK(request_true(&request, "inline"));
	CHECK(!strcmp(request_string(&request, "png"), "/a \"b\""));
	CHECK(request_find(&request, "frames") >= 0 && request.number_count == 3 && request.numbers[2] == 3);
	CHECK(!request_string(&request, "ids") && !request_true(&request, "ids"));
	CHECK(!request_parse("not json", &request));
	CHECK(!request_parse("{\"cmd\":}", &request));
	CHECK(request_parse(" { } ", &request) && request.count == 0);
}

/* a picture written for the test to read back (its pixels: x, y, x ^ y) */
static void png_test(const char *path)
{
	uint32_t width = 300, height = 230, x, y;
	unsigned char *bgra = malloc((size_t)width * height * 4);

	for (y = 0; y < height; y++)
	{
		for (x = 0; x < width; x++)
		{
			unsigned char *pixel = bgra + ((size_t)y * width + x) * 4;

			pixel[2] = (unsigned char)x;
			pixel[1] = (unsigned char)y;
			pixel[0] = (unsigned char)(x ^ y);
			pixel[3] = 0x80;
		}
	}
	CHECK(png_write(bgra, width, height, path));
	{
		uint32_t scaled_height;
		unsigned char *scaled = picture_scale(bgra, width, height, 150, &scaled_height);

		CHECK(scaled && scaled_height == 115);
		/* (the mean of a 2x2 block's) */
		CHECK(scaled && scaled[((size_t)10 * 150 + 20) * 4 + 2] == 41);
		free(scaled);
	}
	free(bgra);
}

/* the pictures' encodings timed on a frame (BGRA bytes, width by height) */
static int bench(const char *path, uint32_t width, uint32_t height)
{
	FILE *file = fopen(path, "rb");
	unsigned char *bgra = malloc((size_t)width * height * 4);
	int kind;

	if (!file || !bgra || fread(bgra, 4, (size_t)width * height, file) != (size_t)width * height)
		return 2;
	fclose(file);
	for (kind = 0; kind < 6; kind++)
	{
		static const char *names[] = { "png stored", "png none+fixed", "png sub+fixed", "png up+fixed",
			"png paeth+fixed", "qoi" };
		double started = seconds_now();
		size_t size = 0;
		unsigned char *bytes = kind == 0 ? png_encode(bgra, width, height, &size) :
			kind == 5 ? qoi_encode(bgra, width, height, &size) :
			png_encode_compressed(bgra, width, height, kind == 1 ? 0 : kind == 2 ? 1 : kind == 3 ? 2 : 4, &size);

		printf("%-16s %9zu bytes  %6.1f ms\n", names[kind], size, (seconds_now() - started) * 1000.0);
		free(bytes);
	}
	return 0;
}

int main(int argc, char *argv[])
{
	if (argc == 5 && !strcmp(argv[1], "bench"))
		return bench(argv[2], (uint32_t)atoi(argv[3]), (uint32_t)atoi(argv[4]));
	if (argc >= 4 && !strcmp(argv[1], "make"))
		return make(argv[2], (uint32_t)atoi(argv[3]), argc > 4 && !strcmp(argv[4], "marks"),
			argc > 4 && !strcmp(argv[4], "old"));
	if (argc == 3 && !strcmp(argv[1], "unit"))
	{
		lz4_test();
		base64_test();
		request_test();
		png_test(argv[2]);
		{
			char folder[4200], *slash;

			snprintf(folder, sizeof(folder), "%s", argv[2]);
			slash = strrchr(folder, '/');
			if (slash)
				*slash = 0;
			deflate_test(slash ? folder : ".");
		}
		fprintf(stderr, "%d failures\n", failures);
		return failures != 0;
	}
	return host_render_replay_main(argc, argv, &fake_platform);
}
