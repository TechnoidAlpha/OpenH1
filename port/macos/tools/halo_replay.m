/*
HALO_REPLAY.M

The renderer's command stream replayed from a recording with the Metal
backend (HALO_RENDER_RECORD: port/android/host/host_render_record.h): the
replay of port/tools/replay/host_render_replay.h, its options and its
service (--serve), with Metal's own:

halo_replay <file> ... [--window] [--cold] [--archive path]

--window: in a window, paced by the display unless --unpaced; --cold: in a
throwaway app of a new bundle id (a new Metal shader cache) with an empty
pipeline archive; --archive: the pipelines' archive (default:
<file>.pipelines).
*/

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

/* (host.h's host_page_size is Mach's too: not used here) */
#define host_page_size host_page_size_of_guest_memory
#include "host.h"
#undef host_page_size
#include "host_render_record.h"
#include "host_render_replay.h"
#include "host_render_stream.h"
#include "xgpu_metal.h"
#include "xgpu_metal_internal.h"
#include "xgpu_metal_stream.h"
#include "xgpu_metal_textures.h"
#include "xgpu_astc.h"

#include <SDL3/SDL.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* host_metal.m */
int host_metal_initialize(void);
int host_metal_window_open(uint32_t window);
void host_metal_set_unpaced(int unpaced);

/* ---------- what the host gives the backend, here */

/* display.astc_textures, from HALO_ASTC_TEXTURES as the host reads it (main) */
char host_astc_textures[16] = "auto";

static SDL_Window *window;

void *host_sdl_handle_get(uint32_t handle, int type)
{
	(void)type;
	return handle == 1 ? (void *)window : NULL;
}

/* ---------- the platform */

/* the textures' cache over the loop: its bytes at the most and at the end
(all, ASTC, DXT decoded), and its counts (xgpu_metal_textures.h) */
static struct xgpu_metal_texture_statistics textures, textures_most;

static void metal_frame_statistics(struct host_render_replay_statistics *to)
{
	struct xgpu_metal_statistics statistics;
	struct xgpu_metal_texture_statistics now;

	xgpu_metal_statistics(&statistics);
	to->cpu_ms = statistics.render_cpu_seconds * 1000.0;
	to->gpu_ms = statistics.gpu_seconds * 1000.0;
	to->compile_ms = statistics.pipeline_seconds * 1000.0;
	to->draws = statistics.draws;
	to->compiles = statistics.pipelines_made;
	to->shader_ms = statistics.shader_seconds * 1000.0;
	to->shaders = statistics.shaders_made;
	xgpu_metal_textures_statistics(&now);
	textures.astc_transcoded += now.astc_transcoded;
	textures.astc_encoded += now.astc_encoded;
	textures.astc_left_decoded += now.astc_left_decoded;
	textures.resident = now.resident;
	textures.resident_astc = now.resident_astc;
	textures.resident_decoded = now.resident_decoded;
	textures.astc_waiting = now.astc_waiting;
	if (now.resident > textures_most.resident)
		textures_most = now;
}

static void metal_report(void)
{
	struct xgpu_astc_statistics astc;

	xgpu_astc_statistics(&astc);
	printf("  textures: at the most %.1f MB (DXT as ASTC %.1f MB, DXT decoded %.1f MB), at the end %.1f MB "
		"(%.1f, %.1f); DXT textures transcoded to ASTC %u, encoded blocks applied %u, left decoded %u, waiting %u; "
		"the encoder: %u jobs, %u from the disk cache, %llu blocks in %.2f s\n",
		textures_most.resident / 1048576.0, textures_most.resident_astc / 1048576.0,
		textures_most.resident_decoded / 1048576.0, textures.resident / 1048576.0,
		textures.resident_astc / 1048576.0, textures.resident_decoded / 1048576.0,
		textures.astc_transcoded, textures.astc_encoded, textures.astc_left_decoded,
		textures.astc_waiting, astc.jobs, astc.cache_hits, (unsigned long long)astc.blocks_encoded,
		astc.encode_seconds);
}

static int metal_initialize(int windowed, int width, int height)
{
	if (!windowed)
		return host_metal_initialize();
	if (!SDL_Init(SDL_INIT_VIDEO) ||
		!(window = SDL_CreateWindow("halo_replay", width / 2 > 640 ? width / 2 : width,
			width / 2 > 640 ? height / 2 : height, SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY)) ||
		!host_metal_window_open(1))
	{
		fprintf(stderr, "halo_replay: no window (%s)\n", SDL_GetError());
		return 0;
	}
	return 1;
}

static void metal_pump(void)
{
	SDL_Event event;

	@autoreleasepool
	{
		while (SDL_PollEvent(&event))
			;
	}
}

static unsigned char *metal_read_target(uint32_t target, uint32_t *width, uint32_t *height)
{
	@autoreleasepool
	{
		return xgpu_metal_stream_read(target, width, height);
	}
}

/* ---------- the ID buffer (host_render_replay.h's ids_*)

Each draw into the target the frame presents is bracketed by the backend
(xgpu_metal_set_draw_inspector: the draw's pass ended before it and after
it): before, the target is copied; after, a compute pass compares it with
the copy, texel by texel, by their bits, and where they differ the draw's
number goes on top of the pixel's stack (depth numbers a pixel, in a
buffer the CPU reads at the present), the others down one, the last let
go; a draw drawn in several parts keeps one place. A multisampled target's
pass ends resolved (its samples stored too), so the resolved texture is
compared. As the Vulkan backend's (xgpu_vulkan.c's xgpu_vulkan_inspect_*). */

static const char inspect_source[] =
	"#include <metal_stdlib>\n"
	"using namespace metal;\n"
	"struct inspect_draw { uint width, height, number, depth; };\n"
	"kernel void inspect_compare(texture2d<float, access::read> before [[texture(0)]],\n"
	"\ttexture2d<float, access::read> after [[texture(1)]], device uint *ids [[buffer(0)]],\n"
	"\tconstant inspect_draw &draw [[buffer(1)]], uint2 at [[thread_position_in_grid]])\n"
	"{\n"
	"\tif (at.x >= draw.width || at.y >= draw.height)\n"
	"\t\treturn;\n"
	"\tif (all(as_type<uint4>(before.read(at)) == as_type<uint4>(after.read(at))))\n"
	"\t\treturn;\n"
	"\tdevice uint *stack = ids + ((ulong)at.y * draw.width + at.x) * draw.depth;\n"
	"\tif (stack[0] == draw.number)\n"
	"\t\treturn;\n"
	"\tfor (uint layer = draw.depth - 1; layer > 0; layer--)\n"
	"\t\tstack[layer] = stack[layer - 1];\n"
	"\tstack[0] = draw.number;\n"
	"}\n";

static struct
{
	/* the target presented (the stream's number), the numbers a pixel, the
	draw handed on (its number plus one; 0 none) */
	uint32_t target, depth, number;
	uint32_t width, height;
	id<MTLComputePipelineState> compare;
	id<MTLBuffer> ids;
	id<MTLTexture> before;
	/* draws compared, and their time (the ID frame's cost) */
	uint32_t draws;
	double started;
} ids;

/* the target's texture now (its handle in *handle; nil if it has none) */
static id<MTLTexture> ids_texture(uint32_t *handle)
{
	*handle = xgpu_metal_stream_target_texture(ids.target);
	return *handle ? xgpu_metal_texture(*handle) : nil;
}

static int ids_size(id<MTLTexture> texture);

static void metal_ids_inspect(int after, uint32_t color, uint32_t level)
{
	uint32_t handle;
	id<MTLTexture> texture;
	id<MTLCommandBuffer> buffer;

	if (!ids.number || level)
		return;
	texture = ids_texture(&handle);
	if (!texture || color != handle)
		return;
	if (!ids.width && !ids_size(texture))
		return;
	if (texture.width != ids.width || texture.height != ids.height)
		return;
	buffer = xgpu_metal_command_buffer();
	if (!after)
	{
		id<MTLBlitCommandEncoder> blit;

		if (!ids.before || ids.before.pixelFormat != texture.pixelFormat)
		{
			MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:texture.pixelFormat
				width:ids.width height:ids.height mipmapped:NO];

			descriptor.usage = MTLTextureUsageShaderRead;
			descriptor.storageMode = MTLStorageModePrivate;
			ids.before = [xgpu_metal_device() newTextureWithDescriptor:descriptor];
			if (!ids.before)
				return;
		}
		blit = [buffer blitCommandEncoder];
		[blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 toTexture:ids.before destinationSlice:0
			destinationLevel:0 sliceCount:1 levelCount:1];
		[blit endEncoding];
	}
	else if (ids.before && ids.before.pixelFormat == texture.pixelFormat)
	{
		id<MTLComputeCommandEncoder> compute = [buffer computeCommandEncoder];
		uint32_t draw[4] = { ids.width, ids.height, ids.number, ids.depth };

		[compute setComputePipelineState:ids.compare];
		[compute setTexture:ids.before atIndex:0];
		[compute setTexture:texture atIndex:1];
		[compute setBuffer:ids.ids offset:0 atIndex:0];
		[compute setBytes:draw length:sizeof(draw) atIndex:1];
		[compute dispatchThreads:MTLSizeMake(ids.width, ids.height, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
		[compute endEncoding];
		ids.draws++;
	}
}

static int metal_ids_begin(uint32_t target, uint32_t depth)
{
	uint32_t handle;
	id<MTLTexture> texture;

	@autoreleasepool
	{
		if (!ids.compare)
		{
			NSError *error = nil;
			id<MTLLibrary> library = [xgpu_metal_device() newLibraryWithSource:@(inspect_source) options:nil error:&error];
			id<MTLFunction> function = [library newFunctionWithName:@"inspect_compare"];

			ids.compare = function ? [xgpu_metal_device() newComputePipelineStateWithFunction:function error:&error] : nil;
			if (!ids.compare)
			{
				fprintf(stderr, "halo_replay: the ID buffer's pass is not made (%s)\n",
					error.localizedDescription.UTF8String);
				return 0;
			}
		}
		ids.target = target;
		ids.depth = depth;
		ids.number = 0;
		ids.draws = 0;
		ids.started = CACurrentMediaTime();
		ids.width = ids.height = 0;
		if (!depth)
			return 0;
		xgpu_metal_set_draw_inspector(metal_ids_inspect);
		/* (a target the frame makes, its first: its buffer made at its
		first draw) */
		texture = ids_texture(&handle);
		return !texture || ids_size(texture);
	}
}

/* the buffer for the target's size, zeroed (no GPU work reads it now: the
last frame's was waited for, and this frame's draws come after) */
static int ids_size(id<MTLTexture> texture)
{
	size_t bytes;

	@autoreleasepool
	{
		bytes = (size_t)texture.width * texture.height * ids.depth * 4;
		if (!ids.ids || ids.ids.length != bytes)
		{
			ids.ids = [xgpu_metal_device() newBufferWithLength:bytes options:MTLResourceStorageModeShared];
			if (!ids.ids)
				return 0;
		}
		memset(ids.ids.contents, 0, bytes);
		ids.width = (uint32_t)texture.width;
		ids.height = (uint32_t)texture.height;
		return 1;
	}
}

static void metal_ids_draw(uint32_t index)
{
	ids.number = index == UINT32_MAX ? 0 : index + 1;
}

static uint32_t *metal_ids_read(uint32_t target, uint32_t *width, uint32_t *height)
{
	uint32_t *copy;
	size_t bytes;

	(void)target;
	@autoreleasepool
	{
		/* (a frame that drew nothing into its target: none of its pixels
drawn) */
		if (!ids.width)
		{
			uint32_t handle;
			id<MTLTexture> texture = ids_texture(&handle);

			if (!texture || !ids_size(texture))
				return NULL;
		}
		xgpu_metal_command_buffer_finish();
		bytes = (size_t)ids.width * ids.height * ids.depth * 4;
		copy = malloc(bytes);
		if (!copy)
			return NULL;
		memcpy(copy, ids.ids.contents, bytes);
		*width = ids.width;
		*height = ids.height;
		if (host_render_replay_verbose)
			fprintf(stderr, "halo_replay: the ID buffer of %u draws compared in %.1f ms\n", ids.draws,
				(CACurrentMediaTime() - ids.started) * 1000.0);
		return copy;
	}
}

static void metal_ids_end(void)
{
	xgpu_metal_set_draw_inspector(NULL);
	ids.number = 0;
}

/* ---------- the cost: the frame's passes timed (xgpu_metal_timing_*), at
the stages' boundaries, the only places Apple's GPUs sample their
timestamps: a draw's own time is the service's subtraction */

static struct host_render_replay_pass_time cost_passes[1024];

static int metal_cost_begin(void)
{
	xgpu_metal_timing_request();
	return 1;
}

static int metal_cost_end(struct host_render_replay_cost *cost)
{
	const struct xgpu_metal_pass_time *passes;
	const uint32_t *draw_pass;
	uint32_t pass_count, draws, index;
	double busy;

	if (!xgpu_metal_timing_wait(10.0, &busy, &passes, &pass_count, &draw_pass, &draws))
		return 0;
	if (pass_count > sizeof(cost_passes) / sizeof(cost_passes[0]))
		pass_count = sizeof(cost_passes) / sizeof(cost_passes[0]);
	for (index = 0; index < pass_count; index++)
	{
		cost_passes[index].kind = passes[index].kind;
		cost_passes[index].width = passes[index].width;
		cost_passes[index].height = passes[index].height;
		cost_passes[index].draws = passes[index].draws;
		cost_passes[index].ms = passes[index].ms;
		cost_passes[index].start_ms = passes[index].start_ms;
		cost_passes[index].vertex_ms = passes[index].vertex_ms;
		cost_passes[index].fragment_ms = passes[index].fragment_ms;
	}
	cost->frame_ms = busy;
	cost->passes = cost_passes;
	cost->pass_count = pass_count;
	cost->draw_pass = draw_pass;
	cost->draw_count = draws;
	cost->note = "Apple's GPUs time passes, not draws: a draw's ms is the frame's time without it subtracted "
		"from its time whole";
	return 1;
}

static const struct host_render_replay_platform metal_platform =
{
	.backend = "metal",
	.window_supported = 1,
	.initialize = metal_initialize,
	.set_unpaced = host_metal_set_unpaced,
	.pump = metal_pump,
	.shutdown = xgpu_metal_shutdown,
	.frame_statistics = metal_frame_statistics,
	.report = metal_report,
	.read_target = metal_read_target,
	.ids_begin = metal_ids_begin,
	.ids_draw = metal_ids_draw,
	.ids_read = metal_ids_read,
	.ids_end = metal_ids_end,
	.cost_begin = metal_cost_begin,
	.cost_draw = xgpu_metal_timing_draw,
	.cost_end = metal_cost_end,
};

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

/* the shared replay's options that take a value */
static int takes_value(const char *option)
{
	return !strcmp(option, "--frames") || !strcmp(option, "--shots") || !strcmp(option, "--shot-frames") ||
		!strcmp(option, "--loop") || !strcmp(option, "--hitch");
}

int main(int argc, char *argv[])
{
	const char *archive = NULL, *path = NULL;
	char archive_default[4200];
	char **arguments = calloc((size_t)argc + 1, sizeof(char *));
	int index, count = 0, cold = 0;

	if (getenv("HALO_ASTC_TEXTURES") && *getenv("HALO_ASTC_TEXTURES"))
		snprintf(host_astc_textures, sizeof(host_astc_textures), "%s", getenv("HALO_ASTC_TEXTURES"));
	if (!arguments)
		return 2;
	/* (Metal's own options taken out; the others are the shared replay's) */
	for (index = 0; index < argc; index++)
	{
		if (index && !strcmp(argv[index], "--cold"))
			cold = 1;
		else if (index && !strcmp(argv[index], "--archive") && index + 1 < argc)
			archive = argv[++index];
		else
		{
			arguments[count++] = argv[index];
			if (index && takes_value(argv[index]) && index + 1 < argc)
				arguments[count++] = argv[++index];
			else if (index && argv[index][0] != '-' && !path)
				path = argv[index];
		}
	}
	if (cold)
	{
		@autoreleasepool
		{
			return cold_run(argc, argv);
		}
	}
	if (path && !(argc > 1 && !strcmp(argv[1], "--compare")))
	{
		if (!archive)
		{
			snprintf(archive_default, sizeof(archive_default), "%s.pipelines", path);
			archive = archive_default;
		}
		setenv("HALO_METAL_ARCHIVE", archive, 1);
	}
	@autoreleasepool
	{
		return host_render_replay_main(count, arguments, &metal_platform);
	}
}
