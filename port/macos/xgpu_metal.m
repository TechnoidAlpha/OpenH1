/*
XGPU_METAL.M

The Metal backend: what xgpu_metal.h declares. Refer to it for what the
backend does; this file says how.

Frames. Each frame is a command buffer (more where a pixel read waits for
the GPU mid-frame), committed by xgpu_metal_frame_end, and its fence is the
serial of its last command buffer, counted by the buffers' completion
handlers (a queue's buffers complete in order). xgpu_metal_frame_begin
waits for the frame two before it, and takes that frame's place in the
three sets of per-frame resources: the upload ring's chunks and the
visibility result buffer.

Passes. set_targets ends the render command encoder (not for the same
targets: the pass, or the clears waiting for its start, go on); the next
draw or clear begins one, with load actions "load", or "clear" for an
attachment that a clear of the whole of it came first for. Any other clear
is a quad drawn through a clear pipeline. Uploads into a texture the GPU may still read are
blits in the command buffer's order (they end the encoder; the pass goes on
with "load"); into one it cannot, they write the texture at once.

Presentation. A frame's picture (the back buffer's texture) is given to the
presentation (host_metal.m) and the back buffer takes another texture, the
picture's kept as its previous: what draws or reads the back buffer before
a clear of the whole of it copies that in first (texture_contents).

Guest data. A guest range read in place is wrapped in a buffer over the
readable host pages around it, up to a granule of 4 MB, and the buffer is
kept until the guest maps those pages again (xgpu_metal_guest_remapped: a
buffer keeps the pages it was made over, and would not see the new ones).
Ranges that cannot be wrapped are copied into the upload ring.
*/

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

/* (host.h's host_page_size is Mach's too: not used here) */
#define host_page_size host_page_size_of_guest_memory
#include "host.h"
#undef host_page_size
#include "host_render_stream.h"
#include "xgpu_metal_internal.h"
#include "xgpu_metal_pipeline.h"
#include "xgpu_metal_combiner.h"

#include <errno.h>
#include <os/lock.h>
#include <float.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define FRAMES_IN_FLIGHT 2
#define FRAME_SLOTS (FRAMES_IN_FLIGHT + 1)
#define TEXTURE_MAXIMUM 16384
#define SHADER_MAXIMUM 8192
/* the upload ring: chunks of 8 MB (more where a frame needs them), the
offsets of chunk n starting at n << RING_CHUNK_SHIFT */
#define RING_CHUNK_SIZE (8u << 20)
#define RING_CHUNK_SHIFT 26
#define RING_CHUNK_MAXIMUM 32
#define RING_ALIGNMENT 256
/* the guest's buffers: at most this many, each inside a granule */
#define GUEST_REGION_MAXIMUM 512
/* the render thread's last buffers found, looked at before the regions */
#define GUEST_RECENT 4
#define GUEST_GRANULE (4u << 20)
/* the visibility result buffers' entries a frame (each a test in one
pass), and their passes: each pass counts into a buffer of its own (Apple's
GPUs zero the first results of a buffer that an earlier pass counted into
when a later pass counts into it too), of VISIBILITY_PASS_ENTRIES entries */
#define VISIBILITY_ENTRIES 16384
#define VISIBILITY_PASSES 512
#define VISIBILITY_PASS_ENTRIES 512

static void metal_log(int priority, NSString *format, ...) NS_FORMAT_FUNCTION(2, 3);

static void metal_log(int priority, NSString *format, ...)
{
	va_list arguments;
	NSString *text;

	va_start(arguments, format);
	text = [[NSString alloc] initWithFormat:format arguments:arguments];
	va_end(arguments);
	host_logf(priority, "metal: %s", text.UTF8String);
}

/* ---------- formats and the interface's values (xgpu_metal_pipeline.m) */

static const struct xgpu_metal_format_information *format_get(uint32_t format)
{
	return xgpu_metal_format_information(format);
}

/* ---------- the backend's state */

struct texture_entry
{
	id<MTLTexture> texture;
	/* a multisampled target's attachment, resolved into texture */
	id<MTLTexture> multisampled;
	struct xgpu_metal_texture_description description;
	/* the last command buffer that read or wrote it */
	uint64_t used;
	/* the last command buffer whose own passes or blits wrote it, and whose
	own draws read it (not those it put aside: the aside passes) */
	uint64_t main_written, main_sampled;
	/* a back buffer given to the presentation (xgpu_metal_frame_end): its
	pictures, which texture (another) has yet to be given, unless a clear of
	the whole of it comes first */
	id<MTLTexture> previous;
	uint8_t present;
	/* (frame-end discards) the frame it was first used in; how many frames
	in a row its first use was a clear of the whole of it, which leaves its
	contents at the end of the frame before unread; the last frame whose
	end discarded them */
	uint64_t first_use_frame, end_discarded_frame;
	uint32_t end_unread_frames;
	/* multisampled is memoryless: its samples never leave tile memory
	(multisampled_demote) */
	BOOL multisampled_memoryless;
	id<MTLTexture> multisampled_next;
	/* the last frame a pass loaded its samples */
	uint64_t samples_loaded_frame;
};

/* a shader: its function, once there is one (made on the making queue:
nil until then); while it is being made, its group */
struct shader_entry
{
	id<MTLFunction> function;
	dispatch_group_t making;
};

struct depth_stencil_key
{
	uint8_t depth_test, depth_function, depth_write;
	uint8_t stencil_test, stencil_function, stencil_fail, stencil_depth_fail, stencil_pass;
	uint8_t stencil_read_mask, stencil_write_mask;
};

struct ring_chunk
{
	id<MTLBuffer> buffer;
	uint32_t size;
};

struct frame_slot
{
	struct ring_chunk chunks[RING_CHUNK_MAXIMUM];
	uint32_t chunk_count;
	uint32_t chunk, position;
	/* the passes' visibility result buffers (made as passes need them),
	and each one's entries */
	id<MTLBuffer> visibility_buffers[VISIBILITY_PASSES];
	uint16_t visibility_pass_entries[VISIBILITY_PASSES];
	uint32_t visibility_passes;
	/* each entry: the visibility test it counts for, its pass's buffer and
	its place in it */
	uint16_t visibility_slots[VISIBILITY_ENTRIES];
	uint16_t visibility_entry_passes[VISIBILITY_ENTRIES];
	uint16_t visibility_entry_offsets[VISIBILITY_ENTRIES];
	/* and the game's number for its test */
	uint32_t visibility_numbers[VISIBILITY_ENTRIES];
	uint32_t visibility_entries;
	uint64_t fence;
	uint64_t number;
};

struct guest_region
{
	uint32_t start, end;
	id<MTLBuffer> buffer;
	uint64_t used;
};

/* what the next draw must set on the encoder */
enum
{
	_dirty_pipeline = 1 << 0,
	_dirty_depth_stencil = 1 << 1,
	_dirty_raster = 1 << 2,
	_dirty_viewport = 1 << 3,
	_dirty_scissor = 1 << 4,
	_dirty_stencil_reference = 1 << 5,
	_dirty_blend_color = 1 << 6,
	_dirty_vertex_constants = 1 << 7,
	_dirty_draw_uniforms = 1 << 8,
	_dirty_textures = 1 << 9,
	_dirty_streams = 1 << 10,
	_dirty_constant_attributes = 1 << 11,
	_dirty_dynamic_lights = 1 << 12,
	_dirty_world_ao = 1 << 13,
	_dirty_all = (1 << 14) - 1,
};

static struct
{
	BOOL ready;
	id<MTLDevice> device;
	id<MTLCommandQueue> queue;
	struct xgpu_metal_options options;
	uint32_t page_size;

	/* the binary archive read at the start, which pipelines are made from;
	the descriptions of the pipelines made or found this run, which the
	archive is written of; whether any are new; the queue writing it */
	id<MTLBinaryArchive> archive;
	NSString *archive_path;
	/* the app's shader library (nil: none), and the one the clears' and the
	presentation's functions are in (it, or theirs compiled) */
	id<MTLLibrary> library, fixed_library;
	NSMutableArray<MTLRenderPipelineDescriptor *> *archived;
	BOOL archive_changed;
	dispatch_queue_t archive_queue;
	/* when a pipeline was last added to it; the saves asked for (one that a
	later one has replaced is not written: the later has its pipelines) */
	double archive_changed_at;
	_Atomic uint32_t archive_saves;

	struct texture_entry textures[TEXTURE_MAXIMUM];
	uint32_t texture_search;
	struct shader_entry shaders[SHADER_MAXIMUM];
	uint32_t shader_count;
	NSMutableDictionary<NSString *, NSNumber *> *shader_handles;
	/* the pipelines made, by their key; those being made, each with the
	group that is left once it is (all under lock) */
	NSMutableDictionary<NSData *, id<MTLRenderPipelineState>> *pipelines;
	NSMutableDictionary<NSData *, dispatch_group_t> *pipelines_making;
	os_unfair_lock lock;
	/* the queue shaders and pipelines are made on (user-initiated: a map's,
	as it loads, or a draw's); the work the loading screen waits for
	(xgpu_metal_prepared) */
	dispatch_queue_t making_queue;
	dispatch_group_t preparing;
	BOOL preparing_draws;
	/* a map's preparation made (before it, a draw waits for its pipeline:
	pipeline_state_get) */
	BOOL prepared_once;
	/* the library the combiner's function is in (the app's, or its source
	compiled where the app has none: the tests) */
	id<MTLLibrary> combiner_library;
	NSMutableDictionary<NSData *, id<MTLDepthStencilState>> *depth_stencil_states;
	NSMutableDictionary<NSData *, id<MTLSamplerState>> *samplers;
	NSMutableDictionary<NSData *, id<MTLRenderPipelineState>> *clear_pipelines;
	id<MTLFunction> clear_vertex, clear_fragment;
	id<MTLDepthStencilState> clear_depth_stencil[4];

	/* command buffers: serials, the last completed */
	id<MTLCommandBuffer> command_buffer;
	/* the aside passes' command buffer, enqueued before the one being
	recorded */
	id<MTLCommandBuffer> aside_buffer;
	/* textures that renamed targets had, kept until the GPU is done with
	them, to be renamed into again */
	NSMutableArray<id<MTLTexture>> *aside_spares;
	NSMutableArray<NSNumber *> *aside_spares_serials;
	uint64_t command_serial;
	uint64_t completed;
	pthread_mutex_t completed_lock;
	pthread_cond_t completed_condition;

	/* frames */
	struct frame_slot slots[FRAME_SLOTS];
	struct frame_slot *slot;
	uint64_t frame_number;
	BOOL in_frame;

	/* the pass */
	uint32_t color, color_level, color_layer, depth;
	/* into the targets' multisampled attachments */
	BOOL multisampled;
	id<MTLRenderCommandEncoder> encoder;
	/* encoder is an aside pass's (on aside_buffer), while the pass it was
	put aside from waits, parked, with its targets */
	BOOL aside;
	/* the frame whose first return to the parked pass ended it */
	uint64_t aside_frame;
	/* the main command buffer's pass (encoder, or the parked one): its
	targets, and which of its attachments' stores are decided as it ends
	(frame-end discards); the frame's last pass is ending */
	uint32_t encoder_color, encoder_depth;
	BOOL encoder_color_deferred, encoder_depth_deferred, encoder_stencil_deferred;
	BOOL frame_ending;
	struct
	{
		id<MTLRenderCommandEncoder> encoder;
		uint32_t color, color_level, color_layer, depth, target_width, target_height;
		BOOL multisampled;
		int32_t visibility_pass, profile_current;
	} parked;
	uint32_t target_width, target_height;
	MTLLoadAction color_load, depth_load, stencil_load;
	MTLClearColor clear_color;
	float clear_depth;
	uint32_t clear_stencil;

	/* the draw state, as set, and what of it the encoder lacks */
	uint32_t dirty;
	struct xgpu_metal_pipeline pipeline;
	BOOL have_pipeline;
	id<MTLRenderPipelineState> pipeline_state;
	struct xgpu_metal_pipeline_state_key pipeline_state_key;
	struct xgpu_metal_dynamic_state dynamic;
	uint32_t stage_textures[XGPU_METAL_TEXTURE_STAGE_COUNT];
	struct xgpu_metal_sampler stage_samplers[XGPU_METAL_TEXTURE_STAGE_COUNT];
	id<MTLSamplerState> stage_sampler_states[XGPU_METAL_TEXTURE_STAGE_COUNT];
	float vertex_constants[XGPU_METAL_VERTEX_CONSTANT_COUNT][4];
	unsigned char draw_uniforms[4096];
	uint32_t draw_uniforms_size;
	struct xgpu_metal_source streams[XGPU_METAL_STREAM_COUNT];
	float constant_attributes[16][4];
	/* the enhanced dynamic lights' block, cells and world */
	struct xgpu_metal_source dynamic_lights[3];
	/* display.ambient_occlusion "world"'s field and block */
	uint32_t world_ao_field, world_ao_table, world_ao_scenery, world_ao_objects, world_ao_pixels;
	struct xgpu_metal_source world_ao_block;

	/* an image for the presentation that a picture is copied into (a
	target of levels or layers) */
	id<MTLTexture> present_copy;

	/* the command stream's ring, wrapped */
	id<MTLBuffer> stream_ring;
	/* the backend's buffers (handle - 1) */
	NSMutableArray<id<MTLBuffer>> *buffers;
	NSMutableArray<NSNumber *> *buffers_used;
	/* the command buffer whose blits last wrote each (aside passes) */
	NSMutableArray<NSNumber *> *buffers_written;
	/* buffers renamed out (xgpu_metal_buffer_write), and the serials of the
	command buffers that last used them */
	NSMutableArray<id<MTLBuffer>> *buffer_spares;
	NSMutableArray<NSNumber *> *buffer_spares_serials;

	/* the visibility test going on: its entry in this pass, and its first
	(its entries get the slot at its end) */
	BOOL visibility_active;
	int32_t visibility_entry;
	/* this pass's visibility result buffer (-1: none) */
	int32_t visibility_pass;
	/* the pass has no entry for it yet: the next draw begins one */
	BOOL visibility_pending;
	uint32_t visibility_first;
	volatile uint32_t *visibility_output;

	/* the guest's buffers */
	struct guest_region regions[GUEST_REGION_MAXIMUM];
	uint32_t region_count;
	pthread_mutex_t region_lock;
	uint64_t region_clock;
	/* counts the guest's remappings: a buffer made while one happened may
	be over the old pages */
	uint64_t remap_generation;
	/* the last depth-stencil state given, and its key: an encoder begun
	again (each return to a parked pass) sets it without a lookup */
	struct depth_stencil_key depth_stencil_key;
	id<MTLDepthStencilState> depth_stencil_state;
	/* (the render thread's, without the lock) the last regions found, each
	good while no remapping has been counted since */
	struct
	{
		uint32_t start, end;
		id<MTLBuffer> buffer;
		uint64_t generation;
	} recent[GUEST_RECENT];
	uint32_t recent_next;

	/* what the passes from now on draw (xgpu_metal_pass_kind) */
	uint32_t pass_kind;

	struct xgpu_metal_statistics statistics;
	struct xgpu_metal_compile_totals compile_totals;
	pthread_mutex_t statistics_lock;
	/* (the render thread's) the draws and passes since the last frame's
	end, added to the statistics there: not each under the lock */
	struct
	{
		uint32_t draws, passes, passes_aside;
	} counted;
} metal = {
	.completed_lock = PTHREAD_MUTEX_INITIALIZER,
	.completed_condition = PTHREAD_COND_INITIALIZER,
	.region_lock = PTHREAD_MUTEX_INITIALIZER,
	.statistics_lock = PTHREAD_MUTEX_INITIALIZER,
};

/* the visibility tests' latest results */
static struct
{
	pthread_mutex_t lock;
	uint32_t samples[XGPU_METAL_VISIBILITY_SLOTS];
	uint64_t frames[XGPU_METAL_VISIBILITY_SLOTS];
	/* the samples a pixel of the game's is, as the last test of the slot
	said */
	float areas[XGPU_METAL_VISIBILITY_SLOTS];
} visibility = { .lock = PTHREAD_MUTEX_INITIALIZER };

static double seconds_now(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

id<MTLDevice> xgpu_metal_device(void)
{
	return metal.device;
}

int xgpu_metal_block_compression(void)
{
	static int pretended = -1;

	/* (HALO_METAL_NO_BC=1: as a GPU without BC, an iPhone's, to measure its
	textures on a Mac) */
	if (pretended < 0)
		pretended = getenv("HALO_METAL_NO_BC") && atoi(getenv("HALO_METAL_NO_BC"));
	return metal.device && metal.device.supportsBCTextureCompression && !pretended;
}

int xgpu_metal_astc(void)
{
	return metal.device && [metal.device supportsFamily:MTLGPUFamilyApple2];
}

int xgpu_metal_simd_reductions(void)
{
	return metal.device && ([metal.device supportsFamily:MTLGPUFamilyApple7] ||
		[metal.device supportsFamily:MTLGPUFamilyMac2]);
}

id<MTLLibrary> xgpu_metal_fixed_library(void)
{
	return metal.fixed_library;
}

id<MTLLibrary> xgpu_metal_library(void)
{
	return metal.library;
}

id<MTLCommandQueue> xgpu_metal_queue(void)
{
	return metal.queue;
}

/* ---------- command buffers and fences */

static void completed_update(uint64_t serial)
{
	pthread_mutex_lock(&metal.completed_lock);
	if (serial > metal.completed)
		metal.completed = serial;
	pthread_cond_broadcast(&metal.completed_condition);
	pthread_mutex_unlock(&metal.completed_lock);
	host_render_stream_gpu_completed();
}

uint64_t xgpu_metal_frame_completed(void)
{
	uint64_t completed;

	pthread_mutex_lock(&metal.completed_lock);
	completed = metal.completed;
	pthread_mutex_unlock(&metal.completed_lock);
	return completed;
}

static _Atomic int frame_waiting;

void xgpu_metal_frame_wait(uint64_t fence)
{
	pthread_mutex_lock(&metal.completed_lock);
	atomic_store(&frame_waiting, 1);
	while (metal.completed < fence)
		pthread_cond_wait(&metal.completed_condition, &metal.completed_lock);
	atomic_store(&frame_waiting, 0);
	pthread_mutex_unlock(&metal.completed_lock);
}

int xgpu_metal_frame_waiting(void)
{
	return atomic_load(&frame_waiting);
}

/* the command buffer being recorded, begun if there is none */
static id<MTLCommandBuffer> command_buffer(void)
{
	if (!metal.command_buffer)
	{
		metal.command_buffer = [metal.queue commandBuffer];
		metal.command_serial++;
	}
	return metal.command_buffer;
}

id<MTLCommandBuffer> xgpu_metal_command_buffer(void)
{
	return command_buffer();
}

/* the serial the command buffer being recorded has or will have */
static uint64_t recording_serial(void)
{
	command_buffer();
	return metal.command_serial;
}

/* ---------- the passes' GPU times and frame captures

HALO_METAL_PASSES=<seconds>: a frame that often is timed pass by pass, with
the GPU's timestamps at each encoder's stages, and logged with each pass's
targets, load and store actions and draws. HALO_METAL_PASS_TIMES=<n>: every
nth frame timed so, each pass's time summed by what it draws (its kind:
xgpu_metal_pass_kind, the kind its first draw had), and the sums logged as a
table every PROFILE_TABLE_FRAMES timed frames. The stages of passes overlap
on the GPU (it runs the vertex stages of later passes ahead), so a render
pass's time is its fragment stage's, from its start, or the end of those
that started before it, to its end: the passes' times add up to the time
the GPU was busy with the frame. A pass draws what follows its first
draw too (the 3D view is one pass, its transparents, water and HUD in it): HALO_METAL_PASS_TIMES_SPLIT=1 ends the timed frames'
passes where the kind changes, at the cost of their stores and loads.
HALO_METAL_CAPTURE=<frame>:<path> (with MTL_CAPTURE_ENABLED=1) writes that
frame and the next, the presentation of the first included, as a GPU trace
document. */

#define PROFILE_PASSES 1024
#define PROFILE_TABLE_FRAMES 20
/* the kinds of passes the backend's kinds are not: aside passes, blits, the
frame's picture's copy for the presentation */
#define PROFILE_KIND_ASIDE XGPU_METAL_PASS_KINDS
#define PROFILE_KIND_COPY (XGPU_METAL_PASS_KINDS + 1)
#define PROFILE_KIND_PRESENT (XGPU_METAL_PASS_KINDS + 2)
#define PROFILE_KINDS (XGPU_METAL_PASS_KINDS + 3)

static const char *const profile_kind_names[PROFILE_KINDS] =
{
	"other", "sky", "world", "transparents", "water", "effects", "hud", "tone map", "anti-aliasing", "panini", "world ao",
	"aside",
	"copies", "present",
};

struct profile_pass
{
	const char *what;
	uint32_t color, depth;
	uint32_t color_format, depth_format, width, height, samples;
	/* colour, depth, stencil (Metal's actions; -1: none) */
	int8_t loads[3], stores[3];
	uint32_t draws, clears;
	/* its kind (PROFILE_KIND_*, or the backend's); the kinds its draws
	had, a bit each */
	uint8_t kind;
	uint32_t kinds;
	/* (HALO_METAL_PASSES_SPLIT: the draw's pipeline and textures) */
	char detail[256];
};

static struct
{
	double interval, last;
	/* 0: none; 1: this frame is timed; 2: its command buffers are being
	done */
	_Atomic int state;
	id<MTLCounterSampleBuffer> samples;
	uint32_t count;
	int32_t current;
	struct profile_pass passes[PROFILE_PASSES];
	MTLTimestamp cpu_start, gpu_start;
	uint64_t first_serial;
	double buffer_seconds;
	uint32_t buffers;
	uint64_t frame;

	/* each draw of a timed frame a pass of its own */
	BOOL split;

	/* HALO_METAL_PASS_TIMES: every how many frames one is timed; its
	passes ended where the kind changes; the timed frames' times by kind
	(ms), their passes and the kinds drawn in them, their busy time, until
	the table is logged */
	uint32_t every;
	BOOL kind_split;
	uint32_t table_frames;
	float table_times[PROFILE_TABLE_FRAMES][PROFILE_KINDS];
	uint32_t table_passes[PROFILE_KINDS], table_kinds[PROFILE_KINDS];
	float table_busy[PROFILE_TABLE_FRAMES];

	uint64_t capture_frame;
	NSString *capture_path;
	BOOL capturing;

	/* xgpu_metal_timing_*: the next frame timed for a caller, this one being
	so; the draw being drawn (its number in the frame, UINT32_MAX none)
	and each draw's pass; the timed frame's passes, given (done) */
	_Atomic int timing_requested;
	BOOL timing;
	uint32_t timing_draw;
	uint32_t *timing_draw_pass;
	uint32_t timing_draws, timing_draw_capacity;
	struct xgpu_metal_pass_time timing_passes[PROFILE_PASSES];
	uint32_t timing_pass_count;
	double timing_busy;
	dispatch_semaphore_t timing_done;
} profile = { .current = -1, .timing_draw = UINT32_MAX };

/* the timestamps' sample buffer, made the first time it is wanted; nil if
the GPU has none at its stages */
static id<MTLCounterSampleBuffer> profile_samples(NSError **error)
{
	MTLCounterSampleBufferDescriptor *descriptor;

	if (profile.samples)
		return profile.samples;
	descriptor = [MTLCounterSampleBufferDescriptor new];
	for (id<MTLCounterSet> set in metal.device.counterSets)
	{
		if ([set.name isEqualToString:MTLCommonCounterSetTimestamp])
			descriptor.counterSet = set;
	}
	descriptor.sampleCount = PROFILE_PASSES * 4;
	descriptor.storageMode = MTLStorageModeShared;
	profile.samples = descriptor.counterSet && [metal.device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary] ?
		[metal.device newCounterSampleBufferWithDescriptor:descriptor error:error] : nil;
	return profile.samples;
}

static void profile_initialize(void)
{
	const char *passes = getenv("HALO_METAL_PASSES"), *capture = getenv("HALO_METAL_CAPTURE");
	const char *times = getenv("HALO_METAL_PASS_TIMES");

	profile.interval = passes ? atof(passes) : 0.0;
	profile.split = getenv("HALO_METAL_PASSES_SPLIT") && atoi(getenv("HALO_METAL_PASSES_SPLIT"));
	profile.every = times && atoi(times) > 0 ? (uint32_t)atoi(times) : 0;
	profile.kind_split = getenv("HALO_METAL_PASS_TIMES_SPLIT") && atoi(getenv("HALO_METAL_PASS_TIMES_SPLIT"));
	profile.timing_done = dispatch_semaphore_create(0);
	if (profile.interval > 0.0 || profile.every)
	{
		NSError *error = nil;

		if (!profile_samples(&error))
		{
			host_logf(HOST_LOG_ERROR, "metal: no timestamps at the GPU's stages: %s",
				error ? error.localizedDescription.UTF8String : "not supported");
			profile.interval = 0.0;
			profile.every = 0;
		}
	}
	if (capture && strchr(capture, ':'))
	{
		profile.capture_frame = strtoull(capture, NULL, 10);
		profile.capture_path = @(strchr(capture, ':') + 1);
	}
}

static void profile_frame_begin(uint64_t frame)
{
	double now = seconds_now();

	if (profile.capture_path && frame == profile.capture_frame)
	{
		MTLCaptureDescriptor *descriptor = [MTLCaptureDescriptor new];
		NSError *error = nil;

		descriptor.captureObject = metal.device;
		descriptor.destination = MTLCaptureDestinationGPUTraceDocument;
		descriptor.outputURL = [NSURL fileURLWithPath:profile.capture_path];
		profile.capturing = [[MTLCaptureManager sharedCaptureManager] startCaptureWithDescriptor:descriptor error:&error];
		host_logf(HOST_LOG_INFO, "metal: capturing frames %llu and %llu into %s%s%s", (unsigned long long)frame,
			(unsigned long long)frame + 1, profile.capture_path.UTF8String, profile.capturing ? "" : ": ",
			profile.capturing ? "" : error.localizedDescription.UTF8String);
	}
	if (atomic_load(&profile.state) != 0 || !((profile.interval > 0.0 && now - profile.last >= profile.interval) ||
		(profile.every && frame % profile.every == 0) || atomic_load(&profile.timing_requested)))
		return;
	profile.timing = atomic_exchange(&profile.timing_requested, 0) != 0;
	profile.timing_draws = 0;
	profile.count = 0;
	profile.current = -1;
	profile.frame = frame;
	profile.buffer_seconds = 0.0;
	profile.buffers = 0;
	/* (the command buffer being recorded, if any, is this frame's) */
	profile.first_serial = metal.command_buffer ? metal.command_serial : metal.command_serial + 1;
	[metal.device sampleTimestamps:&profile.cpu_start gpuTimestamp:&profile.gpu_start];
	atomic_store(&profile.state, 1);
}

/* a pass of the frame timed: its index (-1: none) */
static int32_t profile_pass_add(const char *what)
{
	struct profile_pass *pass;

	if (atomic_load(&profile.state) != 1 || profile.count >= PROFILE_PASSES)
		return -1;
	pass = &profile.passes[profile.count];
	memset(pass, 0, sizeof(*pass));
	memset(pass->loads, -1, sizeof(pass->loads));
	memset(pass->stores, -1, sizeof(pass->stores));
	pass->what = what;
	pass->kind = (uint8_t)metal.pass_kind;
	return (int32_t)profile.count++;
}

/* a blit encoder, timed when the frame is */
static void aside_close(void);

static id<MTLBlitCommandEncoder> blit_encoder(const char *what)
{
	int32_t index;
	MTLBlitPassDescriptor *descriptor;

	aside_close();
	index = profile_pass_add(what);
	if (index < 0)
		return [command_buffer() blitCommandEncoder];
	profile.passes[index].kind = strcmp(what, "present") ? PROFILE_KIND_COPY : PROFILE_KIND_PRESENT;
	descriptor = [MTLBlitPassDescriptor blitPassDescriptor];
	descriptor.sampleBufferAttachments[0].sampleBuffer = profile.samples;
	descriptor.sampleBufferAttachments[0].startOfEncoderSampleIndex = (NSUInteger)index * 4;
	descriptor.sampleBufferAttachments[0].endOfEncoderSampleIndex = (NSUInteger)index * 4 + 1;
	return [command_buffer() blitCommandEncoderWithDescriptor:descriptor];
}

static char action_letter(int action, BOOL store)
{
	if (action < 0)
		return '-';
	if (store)
		return action == MTLStoreActionStore ? 'S' : action == MTLStoreActionDontCare ? 'D' :
			action == MTLStoreActionMultisampleResolve ? 'R' : action == MTLStoreActionStoreAndMultisampleResolve ? 'B' : '?';
	return action == MTLLoadActionLoad ? 'L' : action == MTLLoadActionClear ? 'C' : action == MTLLoadActionDontCare ? 'D' : '?';
}

static int profile_float_compare(const void *a, const void *b)
{
	float x = *(const float *)a, y = *(const float *)b;

	return x < y ? -1 : x > y;
}

/* the timed frame's passes' times from their spans (ms; 0, 0: not timed):
in the order they started, each its time from its start, or the end of
those before it, to its end, into times (each pass's); returns the time the
GPU was busy, their sum */
static double profile_spans(const double *starts, const double *ends, double *times)
{
	static uint32_t order[PROFILE_PASSES];
	double busy_end = 0.0, busy = 0.0;
	uint32_t index, count = 0;

	for (index = 0; index < profile.count; index++)
	{
		uint32_t place = count++;

		times[index] = 0.0;
		if (ends[index] <= 0.0)
		{
			count--;
			continue;
		}
		while (place > 0 && starts[order[place - 1]] > starts[index])
		{
			order[place] = order[place - 1];
			place--;
		}
		order[place] = index;
	}
	for (index = 0; index < count; index++)
	{
		double from = starts[order[index]] > busy_end ? starts[order[index]] : busy_end;

		if (ends[order[index]] > from)
		{
			times[order[index]] = ends[order[index]] - from;
			busy += ends[order[index]] - from;
			busy_end = ends[order[index]];
		}
	}
	return busy;
}

/* (HALO_METAL_PASS_TIMES) the timed frame's passes' times summed by kind,
from their spans (ms; 0, 0: not timed); the table logged once it has
PROFILE_TABLE_FRAMES frames: each kind's median, mean and most a frame,
its passes a frame and the other kinds drawn in its passes */
static void profile_table_add(const double *starts, const double *ends)
{
	static double spans[PROFILE_PASSES];
	float *times, sorted[PROFILE_TABLE_FRAMES];
	double busy;
	uint32_t index, kind, frame;

	if (profile.table_frames >= PROFILE_TABLE_FRAMES)
		return;
	times = profile.table_times[profile.table_frames];
	memset(times, 0, sizeof(profile.table_times[0]));
	busy = profile_spans(starts, ends, spans);
	for (index = 0; index < profile.count; index++)
	{
		const struct profile_pass *pass = &profile.passes[index];

		if (ends[index] <= 0.0)
			continue;
		times[pass->kind] += (float)spans[index];
		profile.table_passes[pass->kind]++;
		profile.table_kinds[pass->kind] |= pass->kinds;
	}
	profile.table_busy[profile.table_frames++] = (float)busy;
	if (profile.table_frames < PROFILE_TABLE_FRAMES)
		return;
	host_logf(HOST_LOG_INFO, "metal: pass times over %u frames, one in %u (ms a frame: median, mean, most; passes a "
		"frame; what else they drew)", PROFILE_TABLE_FRAMES, profile.every);
	for (kind = 0; kind < PROFILE_KINDS; kind++)
	{
		double sum = 0.0;
		char others[160];
		uint32_t other;

		if (!profile.table_passes[kind])
			continue;
		for (frame = 0; frame < PROFILE_TABLE_FRAMES; frame++)
		{
			sorted[frame] = profile.table_times[frame][kind];
			sum += sorted[frame];
		}
		qsort(sorted, PROFILE_TABLE_FRAMES, sizeof(sorted[0]), profile_float_compare);
		others[0] = 0;
		for (other = 0; other < XGPU_METAL_PASS_KINDS; other++)
		{
			if (other != kind && (profile.table_kinds[kind] & (1u << other)) &&
				strlen(others) + strlen(profile_kind_names[other]) + 2 < sizeof(others))
			{
				strcat(others, others[0] ? ", " : "with ");
				strcat(others, profile_kind_names[other]);
			}
		}
		host_logf(HOST_LOG_INFO, "metal:   %-14s %7.3f %7.3f %7.3f %6.1f  %s", profile_kind_names[kind],
			sorted[PROFILE_TABLE_FRAMES / 2], sum / PROFILE_TABLE_FRAMES, sorted[PROFILE_TABLE_FRAMES - 1],
			(double)profile.table_passes[kind] / PROFILE_TABLE_FRAMES, others);
	}
	{
		double sum = 0.0;

		for (frame = 0; frame < PROFILE_TABLE_FRAMES; frame++)
		{
			sorted[frame] = profile.table_busy[frame];
			sum += sorted[frame];
		}
		qsort(sorted, PROFILE_TABLE_FRAMES, sizeof(sorted[0]), profile_float_compare);
		host_logf(HOST_LOG_INFO, "metal:   %-14s %7.3f %7.3f %7.3f", "busy", sorted[PROFILE_TABLE_FRAMES / 2],
			sum / PROFILE_TABLE_FRAMES, sorted[PROFILE_TABLE_FRAMES - 1]);
	}
	profile.table_frames = 0;
	memset(profile.table_passes, 0, sizeof(profile.table_passes));
	memset(profile.table_kinds, 0, sizeof(profile.table_kinds));
}

/* the timed frame's passes, logged (its last command buffer is done) */
static void profile_log(void)
{
	static double starts[PROFILE_PASSES], ends[PROFILE_PASSES], stage_times[PROFILE_PASSES][2];
	NSData *data = [profile.samples resolveCounterRange:NSMakeRange(0, profile.count * 4)];
	const MTLCounterResultTimestamp *samples = data.bytes;
	MTLTimestamp cpu, gpu;
	double scale, first = 0.0, last = 0.0, previous_end = 0.0;
	double vertex_sum = 0.0, fragment_sum = 0.0;
	uint32_t index;

	[metal.device sampleTimestamps:&cpu gpuTimestamp:&gpu];
	/* (nanoseconds a GPU tick) */
	scale = gpu > profile.gpu_start ? (double)(cpu - profile.cpu_start) / (double)(gpu - profile.gpu_start) : 1.0;
	for (index = 0; data && index < profile.count; index++)
	{
		const struct profile_pass *pass = &profile.passes[index];
		uint64_t values[4];
		double start = 0.0, end = 0.0, vertex = 0.0, fragment = 0.0;
		int part;

		for (part = 0; part < 4; part++)
			values[part] = samples[index * 4 + part].timestamp;
		for (part = 0; part < 4; part++)
		{
			double value;

			if (values[part] == MTLCounterErrorValue || !values[part] || (!pass->width && part >= 2))
				continue;
			value = (double)(values[part] - profile.gpu_start) * scale / 1e6;
			if (part % 2 == 0 && (start == 0.0 || value < start))
				start = value;
			if (part % 2 == 1 && value > end)
				end = value;
		}
		if (pass->width)
		{
			if (values[1] > values[0] && values[0] != MTLCounterErrorValue && values[1] != MTLCounterErrorValue)
				vertex = (double)(values[1] - values[0]) * scale / 1e6;
			if (values[3] > values[2] && values[2] != MTLCounterErrorValue && values[3] != MTLCounterErrorValue)
				fragment = (double)(values[3] - values[2]) * scale / 1e6;
		}
		else
		{
			fragment = end > start ? end - start : 0.0;
		}
		stage_times[index][0] = vertex;
		stage_times[index][1] = fragment;
		vertex_sum += vertex;
		fragment_sum += fragment;
		if (first == 0.0 || (start > 0.0 && start < first))
			first = start;
		if (end > last)
			last = end;
		/* (a render pass's time is its fragment stage's: the GPU runs the
		vertex stages of the passes after it ahead of it) */
		starts[index] = start;
		ends[index] = end > start ? end : 0.0;
		if (fragment > 0.0)
		{
			starts[index] = (double)(values[2] - profile.gpu_start) * scale / 1e6;
			ends[index] = starts[index] + fragment;
		}
		if (profile.interval > 0.0)
		{
			host_logf(HOST_LOG_INFO,
				"metal: pass %2u %-7s %4ux%-4u t%u/%u fmt %2u/%2u x%u load %c%c%c store %c%c%c draws %3u clears %2u: "
				"at %7.3f ms, %6.3f long (vertex %6.3f, fragment %6.3f from %7.3f), %6.3f after the last; %s",
				index, pass->what, pass->width, pass->height, pass->color, pass->depth, pass->color_format,
				pass->depth_format, pass->samples,
				action_letter(pass->loads[0], NO), action_letter(pass->loads[1], NO), action_letter(pass->loads[2], NO),
				action_letter(pass->stores[0], YES), action_letter(pass->stores[1], YES),
				action_letter(pass->stores[2], YES), pass->draws, pass->clears, start, end - start, vertex, fragment,
				starts[index], previous_end > 0.0 ? start - previous_end : 0.0, profile_kind_names[pass->kind]);
			if (pass->detail[0])
				host_logf(HOST_LOG_INFO, "metal:    %s", pass->detail);
		}
		if (end > 0.0)
			previous_end = end;
	}
	if (profile.interval > 0.0)
		host_logf(HOST_LOG_INFO,
			"metal: frame %llu timed: %u passes from %.3f to %.3f ms (%.3f ms); stages' sums: vertex %.3f, fragment %.3f; "
			"its %u command buffers %.3f ms",
			(unsigned long long)profile.frame, profile.count, first, last, last - first, vertex_sum, fragment_sum,
			profile.buffers, profile.buffer_seconds * 1000.0);
	if (profile.every && data && !profile.timing)
		profile_table_add(starts, ends);
	if (profile.timing)
	{
		static double spans[PROFILE_PASSES];

		profile.timing_busy = data ? profile_spans(starts, ends, spans) : 0.0;
		profile.timing_pass_count = data ? profile.count : 0;
		for (index = 0; index < profile.timing_pass_count; index++)
		{
			const struct profile_pass *pass = &profile.passes[index];
			struct xgpu_metal_pass_time *to = &profile.timing_passes[index];

			to->kind = pass->kind < PROFILE_KINDS ? profile_kind_names[pass->kind] : "other";
			to->what = pass->what;
			to->draws = pass->draws;
			to->width = pass->width;
			to->height = pass->height;
			to->start_ms = starts[index];
			to->end_ms = ends[index];
			to->ms = spans[index];
			to->vertex_ms = stage_times[index][0];
			to->fragment_ms = stage_times[index][1];
		}
	}
	profile.last = seconds_now();
	atomic_store(&profile.state, 0);
	if (profile.timing)
	{
		profile.timing = NO;
		dispatch_semaphore_signal(profile.timing_done);
	}
}

void xgpu_metal_timing_request(void)
{
	NSError *error = nil;

	if (!profile_samples(&error))
		return;
	while (dispatch_semaphore_wait(profile.timing_done, DISPATCH_TIME_NOW) == 0)
		;
	atomic_store(&profile.timing_requested, 1);
}

void xgpu_metal_timing_draw(uint32_t index)
{
	profile.timing_draw = index;
}

int xgpu_metal_timing_wait(double seconds, double *busy_ms, const struct xgpu_metal_pass_time **passes,
	uint32_t *pass_count, const uint32_t **draw_pass, uint32_t *draws)
{
	if (!profile.samples || dispatch_semaphore_wait(profile.timing_done,
		dispatch_time(DISPATCH_TIME_NOW, (int64_t)(seconds * 1e9))) != 0)
		return 0;
	*busy_ms = profile.timing_busy;
	*passes = profile.timing_passes;
	*pass_count = profile.timing_pass_count;
	*draw_pass = profile.timing_draw_pass;
	*draws = profile.timing_draws;
	return 1;
}

/* ---------- frame-end discards

A tile-based GPU (Apple's: the iPhone's, the iPad's, Apple silicon Macs')
draws each pass in tile memory and stores its attachments into their
textures at the pass's end: tens of megabytes at the screen's size for a
depth and stencil target, four times the colour's for a multisampled one.
Where nothing reads them before they are cleared again, the store is
wasted. The last pass of a frame decides its depth, stencil and
multisampled colour attachments' stores as it ends (deferred store
actions): a target whose first use in each of the last
FRAME_END_DISCARD_FRAMES frames was a clear of the whole of it (not a
load, a draw's read, a copy or a resolve) has its contents at the frame's
end discarded (a multisampled colour is resolved, its samples dropped).
The game clears its depth and its multisampled targets at each frame's
start, so their stores at the frame's end go. A first use that is not a
clear starts the count again (and is logged once, if the contents were
discarded: that frame read what the discard left). The other passes, and
the targets read at the next frame's start, store as before.
HALO_METAL_DISCARDS=0: none. */

#define FRAME_END_DISCARD_FRAMES 30

static struct texture_entry *texture_get(uint32_t texture);

static int discards_enabled = -1;

/* (each use of a texture: a clear of the whole of it, or a use that wants
its contents) */
static void texture_first_use(struct texture_entry *entry, BOOL cleared)
{
	static BOOL logged;

	if (!entry || entry->first_use_frame == metal.frame_number)
		return;
	entry->first_use_frame = metal.frame_number;
	if (cleared)
	{
		if (entry->end_unread_frames < FRAME_END_DISCARD_FRAMES)
			entry->end_unread_frames++;
		return;
	}
	if (entry->end_discarded_frame && entry->end_discarded_frame + 1 == metal.frame_number && !logged)
	{
		host_logf(HOST_LOG_WARN, "metal: a target's contents discarded at a frame's end were read by the next frame "
			"(its stores kept from now on)");
		logged = YES;
	}
	entry->end_unread_frames = 0;
}

/* whether the pass ending now discards the target's contents */
static BOOL frame_end_discards(struct texture_entry *entry)
{
	if (discards_enabled < 0)
		discards_enabled = !getenv("HALO_METAL_DISCARDS") || atoi(getenv("HALO_METAL_DISCARDS")) != 0;
	return discards_enabled && entry && metal.frame_ending && entry->end_unread_frames >= FRAME_END_DISCARD_FRAMES;
}

/* a memoryless multisampled attachment (texture_create) that a pass is
to load or store: it becomes a texture in memory, for the frames after
this one (this pass loads or stores nothing: what it would have read is
lost, once) */
static void multisampled_demote(struct texture_entry *entry, const char *why)
{
	static int logged;
	MTLTextureDescriptor *descriptor;

	if (!entry || !entry->multisampled_memoryless)
		return;
	descriptor = [MTLTextureDescriptor new];
	descriptor.textureType = MTLTextureType2DMultisample;
	descriptor.pixelFormat = entry->multisampled.pixelFormat;
	descriptor.width = entry->multisampled.width;
	descriptor.height = entry->multisampled.height;
	descriptor.sampleCount = entry->multisampled.sampleCount;
	descriptor.usage = MTLTextureUsageRenderTarget;
	descriptor.storageMode = MTLStorageModePrivate;
	entry->multisampled_memoryless = NO;
	entry->end_unread_frames = 0;
	/* (the pass already begun keeps the memoryless one: the next has this) */
	entry->multisampled_next = [metal.device newTextureWithDescriptor:descriptor];
	if (logged < 8)
	{
		host_logf(HOST_LOG_INFO, "metal: a %lux%lu multisampled target's samples are wanted beyond a pass (%s, frame %llu): "
			"kept in memory from now on", (unsigned long)descriptor.width, (unsigned long)descriptor.height, why,
			(unsigned long long)metal.frame_number);
		logged++;
	}
}

static BOOL multisampled_memoryless_supported(void);

/* frames in a row with no pass loading a multisampled target's samples (and
the contents at each end unread) before its samples go back to being
memoryless: a frame's structure changes at a map's start, the loading
screen's passes loading what the game's next frames clear */
#define MEMORYLESS_AGAIN_FRAMES 120

/* (a pass begins on the multisampled attachment, which it loads or not)
the texture in memory that a demotion made, if any; or, the samples
loaded by no pass for long, memoryless again */
static void multisampled_current(struct texture_entry *entry, BOOL loads)
{
	if (!entry)
		return;
	if (loads)
		entry->samples_loaded_frame = metal.frame_number;
	if (entry->multisampled_next)
	{
		entry->multisampled = entry->multisampled_next;
		entry->multisampled_next = nil;
	}
	else if (!loads && !entry->multisampled_memoryless && metal.options.aside_continue &&
		multisampled_memoryless_supported() && metal.frame_number > entry->samples_loaded_frame + MEMORYLESS_AGAIN_FRAMES &&
		entry->end_unread_frames >= FRAME_END_DISCARD_FRAMES)
	{
		MTLTextureDescriptor *descriptor = [MTLTextureDescriptor new];
		id<MTLTexture> memoryless;

		descriptor.textureType = MTLTextureType2DMultisample;
		descriptor.pixelFormat = entry->multisampled.pixelFormat;
		descriptor.width = entry->multisampled.width;
		descriptor.height = entry->multisampled.height;
		descriptor.sampleCount = entry->multisampled.sampleCount;
		descriptor.usage = MTLTextureUsageRenderTarget;
		descriptor.storageMode = MTLStorageModeMemoryless;
		memoryless = [metal.device newTextureWithDescriptor:descriptor];
		if (memoryless)
		{
			static int logged;

			entry->multisampled = memoryless;
			entry->multisampled_memoryless = YES;
			if (logged++ < 8)
				host_logf(HOST_LOG_INFO, "metal: a %lux%lu multisampled target's samples memoryless again (frame %llu)",
					(unsigned long)descriptor.width, (unsigned long)descriptor.height,
					(unsigned long long)metal.frame_number);
		}
	}
}

/* the stores of the pass ending (its attachments' deferred actions:
encoder) */
static void encoder_stores(void)
{
	struct texture_entry *color = texture_get(metal.encoder_color), *depth = texture_get(metal.encoder_depth);

	if (metal.encoder_color_deferred)
	{
		BOOL discard = frame_end_discards(color);

		/* (memoryless samples cannot be stored: they go as the pass ends,
		resolved, and a pass that loads them again keeps them in memory from
		then on, multisampled_demote) */
		if (!discard && color && color->multisampled_memoryless)
		{
			discard = YES;
		}

		[metal.encoder setColorStoreAction:discard ? MTLStoreActionMultisampleResolve :
			MTLStoreActionStoreAndMultisampleResolve atIndex:0];
		if (discard)
			color->end_discarded_frame = metal.frame_number;
		if (profile.current >= 0)
			profile.passes[profile.current].stores[0] = (int8_t)(discard ? MTLStoreActionMultisampleResolve :
				MTLStoreActionStoreAndMultisampleResolve);
	}
	if (metal.encoder_depth_deferred)
	{
		BOOL discard = frame_end_discards(depth);
		MTLStoreAction action;

		if (!discard && depth && depth->multisampled_memoryless && metal.multisampled)
		{
			discard = YES;
		}
		action = discard ? MTLStoreActionDontCare : MTLStoreActionStore;

		[metal.encoder setDepthStoreAction:action];
		if (metal.encoder_stencil_deferred)
			[metal.encoder setStencilStoreAction:action];
		if (discard)
			depth->end_discarded_frame = metal.frame_number;
		if (profile.current >= 0)
		{
			profile.passes[profile.current].stores[1] = (int8_t)action;
			if (metal.encoder_stencil_deferred)
				profile.passes[profile.current].stores[2] = (int8_t)action;
		}
	}
	metal.encoder_color_deferred = metal.encoder_depth_deferred = metal.encoder_stencil_deferred = NO;
}

static void encoder_end(void)
{
	if (metal.encoder)
	{
		if (metal.visibility_active && metal.visibility_entry >= 0)
			[metal.encoder setVisibilityResultMode:MTLVisibilityResultModeDisabled offset:0];
		if (!metal.aside)
			encoder_stores();
		[metal.encoder endEncoding];
		metal.encoder = nil;
		metal.visibility_entry = -1;
		profile.current = -1;
	}
}

/* ---------- aside passes

A pass of a small target (an object's shadow and its blur, a mip chain)
begun while a pass of a large one is open would end that pass: its
targets stored, and loaded again when the game comes back to them, tens
of megabytes each time at the screen's size, which a tile-based GPU's
memory pays for. Where the small pass draws into nothing the command
buffer being recorded has written, and its draws read nothing it has
written, it goes instead into a command buffer of its own enqueued before
that one (aside_buffer), and the large pass, parked meanwhile, goes on
when the game comes back to its targets: the same work, in an order whose
results are the same. A target the command buffer has read is given
another texture first (renamed: its contents copied where the pass loads
them), so that what read it reads what it had. HALO_METAL_ASIDE=0: none. */

/* the open pass's targets this large (the target put aside any size: a
2048x2048 one, the game's, drawn in four passes between parts of the 3D
view, ended the view's pass at every phone's resolution, its multisampled
colour, depth and stencil stored and loaded again; the passes put aside
cost what they did) */
#define ASIDE_LARGE_PIXELS (512 * 512)
#define ASIDE_SPARES 64

static void statistics_add(uint32_t *counter, uint32_t amount);
static struct texture_entry *texture_get(uint32_t texture);

static int aside_enabled = -1;

static id<MTLCommandBuffer> aside_buffer(void)
{
	if (!metal.aside_buffer)
	{
		metal.aside_buffer = [metal.queue commandBuffer];
		[metal.aside_buffer enqueue];
	}
	return metal.aside_buffer;
}

/* commits the aside passes (before the command buffer they were put aside
from) */
static void aside_buffer_commit(void)
{
	if (!metal.aside_buffer)
		return;
	[metal.aside_buffer addCompletedHandler:^(id<MTLCommandBuffer> done) {
		if (done.status == MTLCommandBufferStatusError)
			host_logf(HOST_LOG_ERROR, "metal: a command buffer failed: %s",
				done.error.localizedDescription.UTF8String);
		if (done.GPUEndTime > done.GPUStartTime)
		{
			xgpu_metal_gpu_interval(done.GPUStartTime, done.GPUEndTime);
			pthread_mutex_lock(&metal.statistics_lock);
			metal.statistics.gpu_seconds += done.GPUEndTime - done.GPUStartTime;
			metal.statistics.command_buffers++;
			pthread_mutex_unlock(&metal.statistics_lock);
		}
	}];
	[metal.aside_buffer commit];
	metal.aside_buffer = nil;
}

/* the parked pass made the open one again (its encoder goes on) */
static void aside_unpark(void)
{
	metal.encoder = metal.parked.encoder;
	metal.color = metal.parked.color;
	metal.color_level = metal.parked.color_level;
	metal.color_layer = metal.parked.color_layer;
	metal.depth = metal.parked.depth;
	metal.target_width = metal.parked.target_width;
	metal.target_height = metal.parked.target_height;
	metal.multisampled = metal.parked.multisampled;
	metal.visibility_pass = metal.parked.visibility_pass;
	profile.current = metal.parked.profile_current;
	metal.parked.encoder = nil;
	metal.aside = NO;
	metal.color_load = metal.depth_load = metal.stencil_load = MTLLoadActionLoad;
	metal.dirty = _dirty_all;
	/* (no test went on as it was parked; one begun since counts in it from
	its next draw, as in a pass begun now) */
	metal.visibility_entry = -1;
	metal.visibility_pending = metal.visibility_active;
}

/* ends the aside pass and the pass parked for it: what comes next goes in
the command buffer's order, on the aside pass's targets */
static void aside_close(void)
{
	uint32_t color, color_level, color_layer, depth, width, height;
	BOOL multisampled;

	if (!metal.aside)
		return;
	encoder_end();
	color = metal.color;
	color_level = metal.color_level;
	color_layer = metal.color_layer;
	depth = metal.depth;
	width = metal.target_width;
	height = metal.target_height;
	multisampled = metal.multisampled;
	aside_unpark();
	encoder_end();
	metal.color = color;
	metal.color_level = color_level;
	metal.color_layer = color_layer;
	metal.depth = depth;
	metal.target_width = width;
	metal.target_height = height;
	metal.multisampled = multisampled;
}

/* whether a pass of these targets can be put aside */
static BOOL aside_possible(struct texture_entry *color, uint32_t color_level, BOOL depth, int multisampled)
{
	uint64_t open;

	if (aside_enabled < 0)
		aside_enabled = !getenv("HALO_METAL_ASIDE") || atoi(getenv("HALO_METAL_ASIDE")) != 0;
	if (!aside_enabled || (!metal.encoder && !metal.aside) || !color || depth || multisampled || color->multisampled ||
		color->previous || metal.visibility_active || color->main_written == metal.command_serial)
		return NO;
	open = metal.aside ? (uint64_t)metal.parked.target_width * metal.parked.target_height :
		(uint64_t)metal.target_width * metal.target_height;
	return open >= ASIDE_LARGE_PIXELS;
}

/* a buffer source the command buffer's blits have written */
static BOOL aside_source_written(struct xgpu_metal_source source)
{
	return source.kind == XGPU_METAL_SOURCE_BUFFER && source.handle && source.handle <= metal.buffers_written.count &&
		metal.buffers_written[source.handle - 1].unsignedLongLongValue == metal.command_serial;
}

/* a draw of the aside pass reads what the command buffer has written: a
texture, or a buffer of its vertices or indices */
static BOOL aside_reads_written(struct xgpu_metal_source indices)
{
	int index;

	for (index = 0; index < XGPU_METAL_TEXTURE_STAGE_COUNT; index++)
	{
		struct texture_entry *entry = texture_get(metal.stage_textures[index]);

		if (entry && entry->main_written == metal.command_serial)
			return YES;
	}
	for (index = 0; index < XGPU_METAL_STREAM_COUNT; index++)
	{
		if (aside_source_written(metal.streams[index]))
			return YES;
	}
	return aside_source_written(indices);
}

/* the target another texture (keep: with its contents, which a pass that
clears the whole of one level of one layer need not have); NO if none can
be made */
static BOOL aside_rename(struct texture_entry *entry, BOOL keep)
{
	id<MTLTexture> old = entry->texture, fresh = nil;
	NSUInteger index;

	for (index = 0; index < metal.aside_spares.count; index++)
	{
		id<MTLTexture> spare = metal.aside_spares[index];

		if (metal.aside_spares_serials[index].unsignedLongLongValue <= xgpu_metal_frame_completed() &&
			spare.width == old.width && spare.height == old.height && spare.depth == old.depth &&
			spare.pixelFormat == old.pixelFormat && spare.textureType == old.textureType &&
			spare.mipmapLevelCount == old.mipmapLevelCount && spare.arrayLength == old.arrayLength &&
			spare.usage == old.usage && spare.storageMode == old.storageMode)
		{
			fresh = spare;
			[metal.aside_spares removeObjectAtIndex:index];
			[metal.aside_spares_serials removeObjectAtIndex:index];
			break;
		}
	}
	if (!fresh)
	{
		MTLTextureDescriptor *descriptor = [MTLTextureDescriptor new];

		descriptor.textureType = old.textureType;
		descriptor.pixelFormat = old.pixelFormat;
		descriptor.width = old.width;
		descriptor.height = old.height;
		descriptor.depth = old.depth;
		descriptor.mipmapLevelCount = old.mipmapLevelCount;
		descriptor.arrayLength = old.arrayLength;
		descriptor.sampleCount = old.sampleCount;
		descriptor.usage = old.usage;
		descriptor.storageMode = old.storageMode;
		fresh = [metal.device newTextureWithDescriptor:descriptor];
		if (!fresh)
			return NO;
	}
	if (keep)
	{
		id<MTLBlitCommandEncoder> blit = [aside_buffer() blitCommandEncoder];

		[blit copyFromTexture:old toTexture:fresh];
		[blit endEncoding];
	}
	entry->texture = fresh;
	entry->main_sampled = 0;
	if (!metal.aside_spares)
	{
		metal.aside_spares = [NSMutableArray new];
		metal.aside_spares_serials = [NSMutableArray new];
	}
	[metal.aside_spares addObject:old];
	[metal.aside_spares_serials addObject:@(metal.command_serial)];
	if (metal.aside_spares.count > ASIDE_SPARES)
	{
		[metal.aside_spares removeObjectAtIndex:0];
		[metal.aside_spares_serials removeObjectAtIndex:0];
	}
	statistics_add(&metal.statistics.targets_renamed, 1);
	return YES;
}

static void pass_end(void);
static MTLCompileOptions *compile_options(void);

/* commits the command buffer being recorded; returns its serial */
static uint64_t command_buffer_commit(void (^completed)(void))
{
	id<MTLCommandBuffer> buffer = command_buffer();
	uint64_t serial = metal.command_serial;

	pass_end();
	aside_buffer_commit();
	[buffer addCompletedHandler:^(id<MTLCommandBuffer> done) {
		if (done.status == MTLCommandBufferStatusError)
			host_logf(HOST_LOG_ERROR, "metal: a command buffer failed: %s",
				done.error.localizedDescription.UTF8String);
		if (done.GPUEndTime > done.GPUStartTime)
		{
			xgpu_metal_gpu_interval(done.GPUStartTime, done.GPUEndTime);
			pthread_mutex_lock(&metal.statistics_lock);
			metal.statistics.gpu_seconds += done.GPUEndTime - done.GPUStartTime;
			metal.statistics.command_buffers++;
			pthread_mutex_unlock(&metal.statistics_lock);
			if (atomic_load(&profile.state) && serial >= profile.first_serial)
			{
				profile.buffer_seconds += done.GPUEndTime - done.GPUStartTime;
				profile.buffers++;
			}
		}
		if (completed)
			completed();
		completed_update(serial);
	}];
	[buffer commit];
	metal.command_buffer = nil;
	return serial;
}

/* ---------- start and end */

/* the app's library of the shaders built ahead of time (the renderer's
manifest's: tools/macos_build.py), which a shader is looked up in by its
name before it is compiled */
static void library_open(const char *path)
{
	NSError *error = nil;

	if (!path || !*path)
		return;
	if (![[NSFileManager defaultManager] fileExistsAtPath:@(path)])
	{
		host_logf(HOST_LOG_WARN, "metal: no shader library at %s: each shader is compiled as it is first drawn", path);
		return;
	}
	metal.library = [metal.device newLibraryWithURL:[NSURL fileURLWithPath:@(path)] error:&error];
	if (!metal.library)
		metal_log(HOST_LOG_WARN, @"cannot read the shader library %s (%@): each shader is compiled as it is first drawn",
			path, error.localizedDescription);
}

static void archive_open(void)
{
	MTLBinaryArchiveDescriptor *descriptor = [MTLBinaryArchiveDescriptor new];
	NSError *error = nil;

	if (metal.archive_path && [[NSFileManager defaultManager] fileExistsAtPath:metal.archive_path])
	{
		descriptor.url = [NSURL fileURLWithPath:metal.archive_path];
		metal.archive = [metal.device newBinaryArchiveWithDescriptor:descriptor error:&error];
		if (!metal.archive)
			metal_log(HOST_LOG_WARN, @"cannot read the pipeline archive %@ (%@); starting a new one", metal.archive_path,
				error.localizedDescription);
	}
	if (!metal.archive)
	{
		descriptor.url = nil;
		metal.archive = [metal.device newBinaryArchiveWithDescriptor:descriptor error:&error];
		if (!metal.archive)
			metal_log(HOST_LOG_WARN, @"no pipeline archive: %@", error.localizedDescription);
	}
}

/* how long after the last new pipeline the archive is written while
playing, and how long quitting waits for it (it is only a cache: a save
cut short leaves the archive as it was) */
#define ARCHIVE_SETTLED_SECONDS 10.0
#define ARCHIVE_EXIT_SECONDS 1.0

/* writes the archive of the pipelines made or found this run, on the
archive's queue, and waits for it up to wait seconds (0: not at all,
below 0: until it is written). It is a new archive each time: one that
has been written, or read from a file, and has had pipelines added since
is written without some of their stages (Metal's archiver says so, and
the archive is not written). The pipelines of the archive read at the
start that this run has not made are not kept. Writing it takes a few
milliseconds for each pipeline, so it is written as the pipelines settle
(a map's load, xgpu_metal_prepared; a while after the last made while
drawing, xgpu_metal_frame_end), and quitting does not wait long for it. */
static void archive_save(double wait)
{
	NSArray<MTLRenderPipelineDescriptor *> *pipelines = nil;
	NSString *path = metal.archive_path;
	id<MTLDevice> device = metal.device;
	dispatch_semaphore_t done;
	uint32_t save = 0;

	if (!metal.archive_queue || !path)
		return;
	if (metal.archive_changed)
	{
		os_unfair_lock_lock(&metal.lock);
		metal.archive_changed = NO;
		pipelines = [metal.archived copy];
		os_unfair_lock_unlock(&metal.lock);
		save = atomic_fetch_add(&metal.archive_saves, 1) + 1;
	}
	else if (wait == 0.0)
		return;
	done = dispatch_semaphore_create(0);
	dispatch_async(metal.archive_queue, ^{
		MTLBinaryArchiveDescriptor *descriptor = [MTLBinaryArchiveDescriptor new];
		/* (the process's own: the replay's services on one recording save
		one archive at once) */
		NSString *temporary = [path stringByAppendingFormat:@".new%d", (int)getpid()];
		id<MTLBinaryArchive> archive;
		NSError *error = nil;

		/* (nothing new: the save already asked for waited for; replaced by
		a later one: not written) */
		if (!pipelines || atomic_load(&metal.archive_saves) != save)
		{
			dispatch_semaphore_signal(done);
			return;
		}
		archive = [device newBinaryArchiveWithDescriptor:descriptor error:&error];
		for (MTLRenderPipelineDescriptor *pipeline in pipelines)
		{
			if (archive && ![archive addRenderPipelineFunctionsWithDescriptor:pipeline error:&error])
				break;
		}
		[[NSFileManager defaultManager] createDirectoryAtPath:[path stringByDeletingLastPathComponent]
			withIntermediateDirectories:YES attributes:nil error:nil];
		/* (written beside it, then put in its place: the archive read at the
		start may be mapped from the file) */
		if (!archive || error || ![archive serializeToURL:[NSURL fileURLWithPath:temporary] error:&error] ||
			rename(temporary.fileSystemRepresentation, path.fileSystemRepresentation) != 0)
			metal_log(HOST_LOG_WARN, @"cannot write the pipeline archive %@: %@", path,
				error ? error.localizedDescription : @(strerror(errno)));
		dispatch_semaphore_signal(done);
	});
	if (wait != 0.0)
		host_stall_note("metal: the pipeline archive's writing waited for");
	if (wait != 0.0 && dispatch_semaphore_wait(done, wait < 0.0 ? DISPATCH_TIME_FOREVER :
		dispatch_time(DISPATCH_TIME_NOW, (int64_t)(wait * NSEC_PER_SEC))) != 0)
		host_logf(HOST_LOG_INFO, "metal: the pipeline archive not written: not done after %.1f s", wait);
}

int xgpu_metal_initialize(const struct xgpu_metal_options *options)
{
	NSError *error = nil;
	id<MTLLibrary> library;
	int index;

	if (metal.ready)
		return 1;
	metal.device = MTLCreateSystemDefaultDevice();
	if (!metal.device)
	{
		host_logf(HOST_LOG_ERROR, "metal: no Metal device");
		return 0;
	}
	metal.queue = [metal.device newCommandQueueWithMaxCommandBufferCount:16];
	metal.options = *options;
	metal.page_size = options->page_size ? options->page_size : (uint32_t)getpagesize();
	metal.archive_path = options->archive_path ? @(options->archive_path) : nil;
	metal.options.archive_path = NULL;
	metal.options.library_path = NULL;
	metal.shader_handles = [NSMutableDictionary new];
	metal.pipelines_making = [NSMutableDictionary new];
	metal.lock = OS_UNFAIR_LOCK_INIT;
	metal.making_queue = dispatch_queue_create("OpenH1 shaders and pipelines", dispatch_queue_attr_make_with_qos_class(
		DISPATCH_QUEUE_CONCURRENT, QOS_CLASS_USER_INITIATED, 0));
	metal.preparing = dispatch_group_create();
	metal.pipelines = [NSMutableDictionary new];
	metal.depth_stencil_states = [NSMutableDictionary new];
	metal.samplers = [NSMutableDictionary new];
	metal.clear_pipelines = [NSMutableDictionary new];
	metal.texture_search = 1;
	metal.buffers = [NSMutableArray new];
	metal.buffers_used = [NSMutableArray new];
	metal.buffers_written = [NSMutableArray new];
	metal.visibility_entry = -1;
	archive_open();
	profile_initialize();
	metal.archived = [NSMutableArray new];
	/* (utility's, not background's: at background's the archive took ten
	times as long to write) */
	metal.archive_queue = dispatch_queue_create("OpenH1 pipeline archive", dispatch_queue_attr_make_with_qos_class(
		DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0));

	library_open(options->library_path);
	library = metal.library;
	if (!library || ![library newFunctionWithName:@"xgpu_clear_vertex"])
		library = [metal.device newLibraryWithSource:xgpu_metal_fixed_source options:nil error:&error];
	if (!library)
	{
		metal_log(HOST_LOG_ERROR, @"the clear shaders: %@", error.localizedDescription);
		return 0;
	}
	metal.fixed_library = library;
	metal.combiner_library = metal.library && [metal.library newFunctionWithName:@"xgpu_combiner_fragment"] ?
		metal.library : [metal.device newLibraryWithSource:@(xgpu_metal_combiner_source) options:compile_options()
		error:&error];
	if (!metal.combiner_library)
		metal_log(HOST_LOG_WARN, @"no combiner's function (%@): a program the library lacks is compiled before it is drawn",
			error.localizedDescription);
	metal.clear_vertex = [library newFunctionWithName:@"xgpu_clear_vertex"];
	metal.clear_fragment = [library newFunctionWithName:@"xgpu_clear_fragment"];
	/* depth written or not, stencil replaced or not */
	for (index = 0; index < 4; index++)
	{
		MTLDepthStencilDescriptor *descriptor = [MTLDepthStencilDescriptor new];

		descriptor.depthCompareFunction = MTLCompareFunctionAlways;
		descriptor.depthWriteEnabled = (index & 1) != 0;
		if (index & 2)
		{
			MTLStencilDescriptor *stencil = [MTLStencilDescriptor new];

			stencil.stencilCompareFunction = MTLCompareFunctionAlways;
			stencil.depthStencilPassOperation = MTLStencilOperationReplace;
			stencil.readMask = 0xff;
			stencil.writeMask = 0xff;
			descriptor.frontFaceStencil = stencil;
			descriptor.backFaceStencil = stencil;
		}
		metal.clear_depth_stencil[index] = [metal.device newDepthStencilStateWithDescriptor:descriptor];
	}
	for (index = 0; index < FRAME_SLOTS; index++)
	{
		metal.slots[index].chunks[0].buffer = [metal.device newBufferWithLength:RING_CHUNK_SIZE
			options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
		metal.slots[index].chunks[0].size = RING_CHUNK_SIZE;
		metal.slots[index].chunk_count = 1;
	}
	metal.slot = &metal.slots[0];
	metal.ready = YES;
	host_logf(HOST_LOG_INFO, "metal: %s, pipeline archive %s", metal.device.name.UTF8String,
		metal.archive_path ? metal.archive_path.UTF8String : "none");
	return 1;
}

void xgpu_metal_shutdown(void)
{
	if (!metal.ready)
		return;
	if (metal.command_buffer)
		xgpu_metal_frame_wait(command_buffer_commit(nil));
	xgpu_metal_frame_wait(metal.command_serial);
	archive_save(-1.0);
}

void xgpu_metal_exiting(void)
{
	if (metal.ready)
		archive_save(ARCHIVE_EXIT_SECONDS);
}

void xgpu_metal_capabilities(struct xgpu_metal_capabilities *capabilities)
{
	uint32_t samples = 1;

	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->largest_target = 16384;
	while (samples < 8 && [metal.device supportsTextureSampleCount:samples * 2])
		samples *= 2;
	capabilities->samples = samples;
	capabilities->anisotropy = 16;
	capabilities->border_colors = 1;
	capabilities->base_vertex = 1;
	capabilities->fill_modes = 1;
	snprintf(capabilities->device, sizeof(capabilities->device), "%s (Metal, %s)",
		metal.device ? metal.device.name.UTF8String : "no device",
		NSProcessInfo.processInfo.operatingSystemVersionString.UTF8String);
}

/* the GPU's time on a command buffer, of the game's or the presentation's:
the time the GPU was busy with any is their union (they overlap: the GPU
begins one before the last is done) */
static double gpu_busy_total;

void xgpu_metal_gpu_interval(double start, double end)
{
	static double busy_until;

	pthread_mutex_lock(&metal.statistics_lock);
	if (start < busy_until)
		start = busy_until;
	if (end > start)
	{
		metal.statistics.gpu_busy_seconds += end - start;
		gpu_busy_total += end - start;
	}
	if (end > busy_until)
		busy_until = end;
	pthread_mutex_unlock(&metal.statistics_lock);
}

uint64_t xgpu_metal_gpu_busy_nanoseconds(void)
{
	double total;

	pthread_mutex_lock(&metal.statistics_lock);
	total = gpu_busy_total;
	pthread_mutex_unlock(&metal.statistics_lock);
	return (uint64_t)(total * 1e9);
}

void xgpu_metal_statistics(struct xgpu_metal_statistics *statistics)
{
	pthread_mutex_lock(&metal.statistics_lock);
	*statistics = metal.statistics;
	memset(&metal.statistics, 0, sizeof(metal.statistics));
	pthread_mutex_unlock(&metal.statistics_lock);
}

void xgpu_metal_compile_totals(struct xgpu_metal_compile_totals *totals)
{
	pthread_mutex_lock(&metal.statistics_lock);
	*totals = metal.compile_totals;
	pthread_mutex_unlock(&metal.statistics_lock);
}

static void statistics_add(uint32_t *counter, uint32_t amount);

/* ---------- shaders */

/* the options a shader is compiled with here, as the app's library's are
(tools/macos_shaders.py, METAL_FLAGS) */
static MTLCompileOptions *compile_options(void)
{
	MTLCompileOptions *options = [MTLCompileOptions new];

	/* (safe maths, as the GLSL emitted for the other backends: but the
	shaders' divides whose last bit does not matter are fast::divide, or a
	multiply by a reciprocal, which safe maths would make correctly rounded
	IEEE divides of about 25 instructions each: xgpu_msl.c,
	xgpu_metal_combiner.c) */
	if (@available(macOS 15.0, iOS 18.0, *))
		options.mathMode = MTLMathModeSafe;
	else
		options.fastMathEnabled = NO;
	return options;
}

/* compiles a source's function (named entry); nil (logged) if it does not
compile. Any thread. */
static id<MTLFunction> function_compile(NSString *text, NSString *name)
{
	id<MTLLibrary> library;
	id<MTLFunction> function;
	NSError *error = nil;
	double start = seconds_now(), seconds;

	host_stall_note("metal: a shader compiled");
	library = [metal.device newLibraryWithSource:text options:compile_options() error:&error];
	function = library ? [library newFunctionWithName:name] : nil;
	seconds = seconds_now() - start;
	pthread_mutex_lock(&metal.statistics_lock);
	metal.statistics.shaders_made++;
	metal.statistics.shader_seconds += seconds;
	metal.compile_totals.shaders++;
	metal.compile_totals.shader_seconds += seconds;
	pthread_mutex_unlock(&metal.statistics_lock);
	if (!function)
		metal_log(HOST_LOG_ERROR, @"cannot compile %@: %@", name, error ? error.localizedDescription : @"no such function");
	return function;
}

/* the combiner's function specialised for a program's configuration (any
thread) */
static id<MTLFunction> combiner_specialise(const uint32_t constants[XGPU_METAL_COMBINER_CONSTANTS])
{
	MTLFunctionConstantValues *values = [MTLFunctionConstantValues new];
	id<MTLFunction> function;
	NSError *error = nil;
	double start = seconds_now();
	int index;

	for (index = 0; index < XGPU_METAL_COMBINER_CONSTANTS; index++)
		[values setConstantValue:&constants[index] type:MTLDataTypeUInt atIndex:(NSUInteger)index];
	function = [metal.combiner_library newFunctionWithName:@"xgpu_combiner_fragment" constantValues:values error:&error];
	pthread_mutex_lock(&metal.statistics_lock);
	metal.statistics.shaders_specialised++;
	metal.statistics.shader_seconds += seconds_now() - start;
	pthread_mutex_unlock(&metal.statistics_lock);
	if (!function)
		metal_log(HOST_LOG_ERROR, @"cannot specialise the combiner's function: %@", error.localizedDescription);
	return function;
}

uint32_t xgpu_metal_shader_create(const char *source, size_t size, const char *entry)
{
	return xgpu_metal_pixel_shader_create(source, size, entry, NULL);
}

uint32_t xgpu_metal_pixel_shader_create(const char *source, size_t size, const char *entry,
	const uint32_t *constants)
{
	NSString *name = @(entry), *text;
	NSNumber *existing;
	struct shader_entry *shader;
	id<MTLFunction> function;
	uint32_t handle;
	/* (a block takes a copy of a structure, not of an array) */
	struct { uint32_t values[XGPU_METAL_COMBINER_CONSTANTS]; } copied;

	if (metal.shader_count + 1 >= SHADER_MAXIMUM)
		return 0;
	/* (an entry's name is its source's: xgpu_metal_shaders.c) */
	existing = metal.shader_handles[name];
	if (existing)
		return existing.unsignedIntValue;
	handle = ++metal.shader_count;
	shader = &metal.shaders[handle];
	metal.shader_handles[name] = @(handle);
	function = metal.library ? [metal.library newFunctionWithName:name] : nil;
	if (function)
	{
		shader->function = function;
		statistics_add(&metal.statistics.shaders_from_library, 1);
		return handle;
	}
	/* not in the app's library: made on the making queue, never here. A
	pixel program is the combiner's function specialised for it (the same
	machine code as its own source compiled: port/macos/README.md); one it
	cannot draw (each pixel's lighting, a debugging setting's) is compiled */
	if (constants && metal.combiner_library)
	{
		memcpy(copied.values, constants, sizeof(copied.values));
		if (metal.options.synchronous)
		{
			shader->function = combiner_specialise(copied.values);
			return handle;
		}
		shader->making = dispatch_group_create();
		dispatch_group_async(shader->making, metal.making_queue, ^{
			id<MTLFunction> specialised = combiner_specialise(copied.values);

			os_unfair_lock_lock(&metal.lock);
			metal.shaders[handle].function = specialised;
			os_unfair_lock_unlock(&metal.lock);
		});
	}
	else
	{
		text = [[NSString alloc] initWithBytes:source length:size encoding:NSUTF8StringEncoding];
		if (!text)
			return 0;
		host_logf(HOST_LOG_INFO, "metal: %s is not in the app's library: compiled", entry);
		if (metal.options.synchronous)
		{
			shader->function = function_compile(text, name);
			return handle;
		}
		shader->making = dispatch_group_create();
		dispatch_group_async(shader->making, metal.making_queue, ^{
			id<MTLFunction> compiled = function_compile(text, name);

			os_unfair_lock_lock(&metal.lock);
			metal.shaders[handle].function = compiled;
			os_unfair_lock_unlock(&metal.lock);
		});
	}
	return handle;
}

/* a shader's function (nil while it is made; any thread) */
static id<MTLFunction> shader_function(uint32_t shader)
{
	id<MTLFunction> function = nil;

	if (!shader || shader > metal.shader_count)
		return nil;
	os_unfair_lock_lock(&metal.lock);
	function = metal.shaders[shader].function;
	os_unfair_lock_unlock(&metal.lock);
	return function;
}

/* ---------- textures */

static struct texture_entry *texture_get(uint32_t texture)
{
	return texture && texture < TEXTURE_MAXIMUM && metal.textures[texture].present ? &metal.textures[texture] : NULL;
}

static void texture_contents(struct texture_entry *entry);

id<MTLTexture> xgpu_metal_texture(uint32_t texture)
{
	struct texture_entry *entry = texture_get(texture);

	texture_contents(entry);
	return entry ? entry->texture : nil;
}

/* a new target's first contents, black (depth 1, stencil 0): Apple's GPUs
give a new private texture none of its own (an M1 Max's read as magenta),
and the game may present or sample a target before it draws into it (its
first frame presents the back buffer as the device made it) */
static void texture_clear_new(id<MTLTexture> texture)
{
	MTLPixelFormat format = texture.pixelFormat;
	BOOL depth = format == MTLPixelFormatDepth32Float_Stencil8 || format == MTLPixelFormatDepth32Float ||
		format == MTLPixelFormatDepth16Unorm || format == MTLPixelFormatX32_Stencil8;
	BOOL stencil = format == MTLPixelFormatDepth32Float_Stencil8;
	NSUInteger slices = texture.textureType == MTLTextureTypeCube ? 6 :
		texture.textureType == MTLTextureType2DArray ? texture.arrayLength : 1, slice;

	if (texture.textureType == MTLTextureType3D)
		return;
	if (!metal.in_frame)
		xgpu_metal_frame_begin();
	/* (not pass_end: the clears waiting for the pass's start still wait,
	for the targets they are of) */
	encoder_end();
	for (slice = 0; slice < slices; slice++)
	{
		MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];

		if (depth)
		{
			pass.depthAttachment.texture = texture;
			pass.depthAttachment.slice = slice;
			pass.depthAttachment.loadAction = MTLLoadActionClear;
			pass.depthAttachment.storeAction = MTLStoreActionStore;
			/* (the far plane: depth reversed, xgpu_backend.h) */
			pass.depthAttachment.clearDepth = 0.0;
			if (stencil)
			{
				pass.stencilAttachment.texture = texture;
				pass.stencilAttachment.slice = slice;
				pass.stencilAttachment.loadAction = MTLLoadActionClear;
				pass.stencilAttachment.storeAction = MTLStoreActionStore;
				pass.stencilAttachment.clearStencil = 0;
			}
		}
		else
		{
			pass.colorAttachments[0].texture = texture;
			pass.colorAttachments[0].slice = slice;
			pass.colorAttachments[0].loadAction = MTLLoadActionClear;
			pass.colorAttachments[0].storeAction = MTLStoreActionStore;
			pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
		}
		aside_close();
		[[command_buffer() renderCommandEncoderWithDescriptor:pass] endEncoding];
	}
}

/* whether multisampled attachments are made memoryless (Apple's GPUs: every
iOS device's, Apple silicon Macs'), with HALO_METAL_MEMORYLESS=1 alone:
the game ends and resumes its 3D view's pass now and then (every few
hundred frames in a10), and a memoryless target's samples are gone then,
which shows for a frame (multisampled_demote). The frame-end discards
spare the bandwidth without that; memoryless spares the memory too
(about 130 MB at an iPhone Pro Max's resolution with MSAA 4x). */
static BOOL multisampled_memoryless_supported(void)
{
	static int supported = -1;

	if (supported < 0)
		supported = [metal.device supportsFamily:MTLGPUFamilyApple1] && getenv("HALO_METAL_MEMORYLESS") &&
			atoi(getenv("HALO_METAL_MEMORYLESS")) != 0;
	return supported != 0;
}

uint32_t xgpu_metal_texture_create(const struct xgpu_metal_texture_description *description)
{
	const struct xgpu_metal_format_information *format = format_get(description->format);
	MTLTextureDescriptor *descriptor;
	struct texture_entry *entry;
	uint32_t handle, tries;

	if (format->pixel_format == MTLPixelFormatInvalid || !description->width || !description->height)
	{
		host_logf(HOST_LOG_ERROR, "metal: cannot make a texture of format %u, %ux%u", description->format,
			description->width, description->height);
		return 0;
	}
	for (handle = 0, tries = 1; tries < TEXTURE_MAXIMUM; tries++)
	{
		uint32_t candidate = metal.texture_search;

		metal.texture_search = metal.texture_search + 1 < TEXTURE_MAXIMUM ? metal.texture_search + 1 : 1;
		if (!metal.textures[candidate].present)
		{
			handle = candidate;
			break;
		}
	}
	if (!handle)
	{
		host_logf(HOST_LOG_ERROR, "metal: out of texture handles");
		return 0;
	}
	descriptor = [MTLTextureDescriptor new];
	descriptor.pixelFormat = format->pixel_format;
	descriptor.width = description->width;
	descriptor.height = description->height;
	descriptor.mipmapLevelCount = description->levels ? description->levels : 1;
	switch (description->type)
	{
	case XGPU_METAL_TEXTURE_3D:
		descriptor.textureType = MTLTextureType3D;
		descriptor.depth = description->depth_or_layers ? description->depth_or_layers : 1;
		break;
	case XGPU_METAL_TEXTURE_CUBE:
		descriptor.textureType = MTLTextureTypeCube;
		break;
	case XGPU_METAL_TEXTURE_2D_ARRAY:
		descriptor.textureType = MTLTextureType2DArray;
		descriptor.arrayLength = description->depth_or_layers ? description->depth_or_layers : 1;
		break;
	default:
		descriptor.textureType = MTLTextureType2D;
		break;
	}
	descriptor.usage = MTLTextureUsageShaderRead;
	if (description->usage & XGPU_METAL_USAGE_TARGET)
	{
		/* (drawn into, and copied out of by blits: private) */
		descriptor.usage |= MTLTextureUsageRenderTarget;
		descriptor.storageMode = MTLStorageModePrivate;
	}
	else
	{
		/* (written by the CPU where the GPU is not reading it) */
		descriptor.storageMode = MTLStorageModeShared;
	}
	if (description->channels & 0x100)
	{
		/* (each channel taken from another of the format's) */
		const MTLTextureSwizzle from[4] = { format->swizzle.red, format->swizzle.green, format->swizzle.blue,
			format->swizzle.alpha };

		descriptor.swizzle = (MTLTextureSwizzleChannels){ from[description->channels & 3],
			from[(description->channels >> 2) & 3], from[(description->channels >> 4) & 3],
			from[(description->channels >> 6) & 3] };
	}
	else if (format->swizzled)
		descriptor.swizzle = format->swizzle;
	entry = &metal.textures[handle];
	entry->texture = [metal.device newTextureWithDescriptor:descriptor];
	entry->multisampled = nil;
	if (entry->texture && description->samples > 1 && (description->usage & XGPU_METAL_USAGE_TARGET))
	{
		MTLTextureDescriptor *multisampled = [MTLTextureDescriptor new];

		multisampled.textureType = MTLTextureType2DMultisample;
		multisampled.pixelFormat = format->pixel_format;
		multisampled.width = description->width;
		multisampled.height = description->height;
		multisampled.sampleCount = description->samples;
		multisampled.usage = MTLTextureUsageRenderTarget;
		multisampled.storageMode = MTLStorageModePrivate;
		/* (with the 3D view in one pass, its samples need never leave
		tile memory: none of memory's, which is a lot at a phone's
		resolution, until a pass would load or store them) */
		entry->multisampled_memoryless = metal.options.aside_continue && multisampled_memoryless_supported();
		if (entry->multisampled_memoryless)
			multisampled.storageMode = MTLStorageModeMemoryless;
		entry->multisampled_next = nil;
		entry->multisampled = [metal.device newTextureWithDescriptor:multisampled];
		if (!entry->multisampled)
			entry->texture = nil;
	}
	if (!entry->texture)
	{
		host_logf(HOST_LOG_ERROR, "metal: cannot make a %ux%u texture of format %u", description->width,
			description->height, description->format);
		return 0;
	}
	entry->description = *description;
	entry->used = 0;
	entry->previous = nil;
	entry->present = 1;
	if (description->usage & XGPU_METAL_USAGE_TARGET)
	{
		texture_clear_new(entry->texture);
		entry->main_written = recording_serial();
		if (entry->multisampled && !entry->multisampled_memoryless)
			texture_clear_new(entry->multisampled);
		entry->used = recording_serial();
	}
	return handle;
}

void xgpu_metal_texture_destroy(uint32_t texture)
{
	struct texture_entry *entry = texture_get(texture);
	int stage;

	if (!entry)
		return;
	/* (the command buffers that use it keep it until they are done) */
	entry->texture = nil;
	entry->multisampled = nil;
	entry->previous = nil;
	entry->present = 0;
	for (stage = 0; stage < XGPU_METAL_TEXTURE_STAGE_COUNT; stage++)
	{
		if (metal.stage_textures[stage] == texture)
			metal.stage_textures[stage] = 0;
	}
}


void xgpu_metal_texture_make_levels(uint32_t texture)
{
	struct texture_entry *entry = texture_get(texture);
	id<MTLBlitCommandEncoder> blit;

	if (!entry || entry->texture.mipmapLevelCount < 2)
		return;
	if (!metal.in_frame)
		xgpu_metal_frame_begin();
	texture_contents(entry);
	pass_end();
	blit = blit_encoder("levels");
	[blit generateMipmapsForTexture:entry->texture];
	[blit endEncoding];
	entry->used = entry->main_written = recording_serial();
}

void xgpu_metal_texture_copy(uint32_t source, uint32_t destination)
{
	struct texture_entry *from = texture_get(source), *to = texture_get(destination);
	id<MTLBlitCommandEncoder> blit;
	NSUInteger width, height;

	if (!from || !to || from->texture.pixelFormat != to->texture.pixelFormat)
		return;
	texture_contents(from);
	texture_contents(to);
	width = from->texture.width < to->texture.width ? from->texture.width : to->texture.width;
	height = from->texture.height < to->texture.height ? from->texture.height : to->texture.height;
	if (!metal.in_frame)
		xgpu_metal_frame_begin();
	pass_end();
	blit = blit_encoder("copy");
	[blit copyFromTexture:from->texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
		sourceSize:MTLSizeMake(width, height, 1) toTexture:to->texture destinationSlice:0 destinationLevel:0
		destinationOrigin:MTLOriginMake(0, 0, 0)];
	[blit endEncoding];
	from->used = to->used = from->main_sampled = to->main_written = recording_serial();
	texture_first_use(from, NO);
	texture_first_use(to, NO);
	statistics_add(&metal.statistics.feedback_copies, 1);
}

void xgpu_metal_texture_resolve(uint32_t texture)
{
	struct texture_entry *entry = texture_get(texture);
	const struct xgpu_metal_format_information *format;
	MTLRenderPassDescriptor *pass;
	id<MTLRenderCommandEncoder> render;

	if (!entry || !entry->multisampled)
		return;
	if (!metal.in_frame)
		xgpu_metal_frame_begin();
	pass_end();
	/* (memoryless samples, gone with the pass that drew them: kept from
	now on, and the resolve's own source the texture in memory) */
	multisampled_demote(entry, "a resolve");
	multisampled_current(entry, YES);
	format = format_get(entry->description.format);
	/* (an empty pass that loads the samples and resolves them) */
	pass = [MTLRenderPassDescriptor renderPassDescriptor];
	if (format->depth)
	{
		pass.depthAttachment.texture = entry->multisampled;
		pass.depthAttachment.resolveTexture = entry->texture;
		pass.depthAttachment.loadAction = MTLLoadActionLoad;
		pass.depthAttachment.storeAction = MTLStoreActionStoreAndMultisampleResolve;
	}
	else
	{
		pass.colorAttachments[0].texture = entry->multisampled;
		pass.colorAttachments[0].resolveTexture = entry->texture;
		pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
		pass.colorAttachments[0].storeAction = MTLStoreActionStoreAndMultisampleResolve;
	}
	render = [command_buffer() renderCommandEncoderWithDescriptor:pass];
	[render endEncoding];
	texture_first_use(entry, NO);
	entry->used = entry->main_written = recording_serial();
}

static void statistics_add(uint32_t *counter, uint32_t amount)
{
	pthread_mutex_lock(&metal.statistics_lock);
	*counter += amount;
	pthread_mutex_unlock(&metal.statistics_lock);
}

/* a back buffer given to the presentation at the last frame's end: the
pictures it had, copied into the texture in its place before anything but
a clear of the whole of it uses it */
static void texture_contents(struct texture_entry *entry)
{
	id<MTLBlitCommandEncoder> blit;

	if (!entry || !entry->previous)
		return;
	if (!metal.in_frame)
		xgpu_metal_frame_begin();
	/* (not pass_end: the clears waiting for the pass's start still wait,
	for the targets they are of) */
	encoder_end();
	blit = blit_encoder("restore");
	[blit copyFromTexture:entry->previous toTexture:entry->texture];
	[blit endEncoding];
	entry->previous = nil;
	entry->used = entry->main_written = recording_serial();
	statistics_add(&metal.statistics.pictures_restored, 1);
}

/* (the ring: below) */
static id<MTLBuffer> ring_buffer(struct xgpu_metal_source source, NSUInteger *offset);

void xgpu_metal_texture_upload(uint32_t texture, uint32_t level, uint32_t layer, uint32_t x, uint32_t y, uint32_t z,
	uint32_t width, uint32_t height, uint32_t depth, const void *data, uint32_t bytes_per_row,
	uint32_t bytes_per_image)
{
	struct texture_entry *entry = texture_get(texture);
	const struct xgpu_metal_format_information *format;
	MTLRegion region;
	uint32_t rows, size;

	if (!entry || !width || !height)
		return;
	texture_contents(entry);
	format = format_get(entry->description.format);
	if (!depth)
		depth = 1;
	rows = format->compressed ? (height + 3) / 4 : height;
	if (!bytes_per_image)
		bytes_per_image = bytes_per_row * rows;
	size = bytes_per_image * (depth - 1) + bytes_per_row * (rows - 1) +
		(format->compressed ? (width + 3) / 4 * format->bytes : width * format->bytes);
	region = MTLRegionMake3D(x, y, z, width, height, depth);
	if (entry->texture.storageMode == MTLStorageModeShared && entry->used <= xgpu_metal_frame_completed())
	{
		/* nothing the GPU has to do reads it: written now */
		[entry->texture replaceRegion:region mipmapLevel:level slice:layer withBytes:data bytesPerRow:bytes_per_row
			bytesPerImage:entry->description.type == XGPU_METAL_TEXTURE_3D ? bytes_per_image : 0];
		statistics_add(&metal.statistics.uploads_direct, 1);
	}
	else
	{
		/* in the command buffer's order: a blit from the ring */
		struct xgpu_metal_source source = xgpu_metal_stream(data, size);
		NSUInteger offset;
		id<MTLBuffer> buffer = ring_buffer(source, &offset);
		id<MTLBlitCommandEncoder> blit;

		if (!buffer)
			return;
		pass_end();
		blit = blit_encoder("upload");
		[blit copyFromBuffer:buffer sourceOffset:offset sourceBytesPerRow:bytes_per_row
			sourceBytesPerImage:entry->description.type == XGPU_METAL_TEXTURE_3D ? bytes_per_image : 0
			sourceSize:MTLSizeMake(width, height, depth) toTexture:entry->texture destinationSlice:layer
			destinationLevel:level destinationOrigin:MTLOriginMake(x, y, z)];
		[blit endEncoding];
		entry->used = entry->main_written = recording_serial();
		statistics_add(&metal.statistics.uploads_ordered, 1);
	}
}

/* ---------- the upload ring */

struct xgpu_metal_source xgpu_metal_stream(const void *data, uint32_t size)
{
	struct frame_slot *slot;
	struct xgpu_metal_source source = { XGPU_METAL_SOURCE_NONE, 0, 0, 0 };
	uint32_t position;

	if (!size || size > (1u << RING_CHUNK_SHIFT))
		return source;
	/* (the data is the frame's: one begins, if none has, before it takes
	ring space whose fence it sets) */
	if (!metal.in_frame)
		xgpu_metal_frame_begin();
	slot = metal.slot;
	position = (slot->position + RING_ALIGNMENT - 1) & ~(uint32_t)(RING_ALIGNMENT - 1);
	if (position + size > slot->chunks[slot->chunk].size)
	{
		/* the next chunk, made where there is none or it is too small */
		uint32_t next = slot->chunk + 1;

		if (next >= RING_CHUNK_MAXIMUM)
		{
			host_logf(HOST_LOG_ERROR, "metal: the frame's upload ring is full");
			return source;
		}
		if (next >= slot->chunk_count || slot->chunks[next].size < size)
		{
			uint32_t chunk_size = size > RING_CHUNK_SIZE ? size : RING_CHUNK_SIZE;

			slot->chunks[next].buffer = [metal.device newBufferWithLength:chunk_size
				options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
			slot->chunks[next].size = chunk_size;
			if (next >= slot->chunk_count)
				slot->chunk_count = next + 1;
		}
		slot->chunk = next;
		position = 0;
	}
	memcpy((unsigned char *)slot->chunks[slot->chunk].buffer.contents + position, data, size);
	slot->position = position + size;
	source.kind = XGPU_METAL_SOURCE_RING;
	source.offset = (slot->chunk << RING_CHUNK_SHIFT) + position;
	source.size = size;
	pthread_mutex_lock(&metal.statistics_lock);
	metal.statistics.ring_bytes += size;
	pthread_mutex_unlock(&metal.statistics_lock);
	return source;
}

static id<MTLBuffer> ring_buffer(struct xgpu_metal_source source, NSUInteger *offset)
{
	uint32_t chunk = source.offset >> RING_CHUNK_SHIFT;

	if (source.kind != XGPU_METAL_SOURCE_RING || chunk >= metal.slot->chunk_count)
		return nil;
	*offset = source.offset & ((1u << RING_CHUNK_SHIFT) - 1);
	return metal.slot->chunks[chunk].buffer;
}

/* ---------- the guest's memory, read in place */

struct xgpu_metal_source xgpu_metal_guest(uint32_t address, uint32_t size)
{
	struct xgpu_metal_source source = { XGPU_METAL_SOURCE_GUEST, address, size };

	return source;
}

void xgpu_metal_guest_remapped(uint32_t address, uint32_t size)
{
	uint64_t end = (uint64_t)address + size;
	uint32_t index;

	pthread_mutex_lock(&metal.region_lock);
	__atomic_store_n(&metal.remap_generation, metal.remap_generation + 1, __ATOMIC_RELEASE);
	for (index = 0; index < metal.region_count;)
	{
		struct guest_region *region = &metal.regions[index];

		if (region->start < end && address < region->end)
		{
			/* (a command buffer that uses it keeps it, and the pages it
			had, until it is done) */
			metal.region_count--;
			metal.regions[index] = metal.regions[metal.region_count];
			metal.regions[metal.region_count].buffer = nil;
		}
		else
		{
			index++;
		}
	}
	pthread_mutex_unlock(&metal.region_lock);
}

/* (the render thread) a region found, remembered for the next lookups */
static void guest_recent_add(uint32_t start, uint32_t end, id<MTLBuffer> buffer, uint64_t generation)
{
	uint32_t index = metal.recent_next++ % GUEST_RECENT;

	metal.recent[index].start = start;
	metal.recent[index].end = end;
	metal.recent[index].buffer = buffer;
	metal.recent[index].generation = generation;
}

/* (the render thread, the region lock held) a region let go: not
remembered either */
static void guest_recent_forget(id<MTLBuffer> buffer)
{
	uint32_t index;

	for (index = 0; index < GUEST_RECENT; index++)
	{
		if (metal.recent[index].buffer == buffer)
			metal.recent[index].buffer = nil;
	}
}

/* a buffer over the guest range and the range's offset in it, or nil */
static id<MTLBuffer> guest_buffer(uint32_t address, uint32_t size, NSUInteger *offset)
{
	uint64_t end = (uint64_t)address + size;
	uint32_t index, start, extent_end, limit_start;
	uint64_t limit_end;
	struct guest_region *region;
	id<MTLBuffer> buffer = nil;
	uint64_t generation;

	/* (most draws read where one of the last few did: no lock, no search,
	while the guest has remapped nothing; a remapping after this look is
	as one after the lock below is let go) */
	generation = __atomic_load_n(&metal.remap_generation, __ATOMIC_ACQUIRE);
	for (index = 0; index < GUEST_RECENT; index++)
	{
		if (metal.recent[index].buffer && metal.recent[index].generation == generation &&
			metal.recent[index].start <= address && end <= metal.recent[index].end)
		{
			*offset = address - metal.recent[index].start;
			return metal.recent[index].buffer;
		}
	}

	pthread_mutex_lock(&metal.region_lock);
	metal.region_clock++;
	generation = metal.remap_generation;
	for (index = 0; index < metal.region_count; index++)
	{
		region = &metal.regions[index];
		if (region->start <= address && end <= region->end)
		{
			region->used = metal.region_clock;
			*offset = address - region->start;
			buffer = region->buffer;
			guest_recent_add(region->start, region->end, buffer, generation);
			break;
		}
	}
	pthread_mutex_unlock(&metal.region_lock);
	if (buffer || !metal.options.guest_extent || !size)
		return buffer;

	/* a new one: the readable pages about the range, inside its granules */
	limit_start = address & ~(GUEST_GRANULE - 1);
	limit_end = (end + GUEST_GRANULE - 1) & ~(uint64_t)(GUEST_GRANULE - 1);
	if (limit_end > 0xffffffffull)
		limit_end = 0x100000000ull - metal.page_size;
	if (!metal.options.guest_extent(address, size, limit_start, (uint32_t)limit_end, &start, &extent_end) ||
		start > address || extent_end < end || ((start | extent_end) & (metal.page_size - 1)))
		return nil;
	buffer = [metal.device newBufferWithBytesNoCopy:(void *)host_address(start) length:extent_end - start
		options:MTLResourceStorageModeShared deallocator:nil];
	if (!buffer)
		return nil;
	statistics_add(&metal.statistics.guest_buffers_made, 1);
	pthread_mutex_lock(&metal.region_lock);
	if (generation != metal.remap_generation)
	{
		pthread_mutex_unlock(&metal.region_lock);
		return nil;
	}
	if (metal.region_count == GUEST_REGION_MAXIMUM)
	{
		/* the least recently used goes */
		uint32_t oldest = 0;

		for (index = 1; index < metal.region_count; index++)
		{
			if (metal.regions[index].used < metal.regions[oldest].used)
				oldest = index;
		}
		guest_recent_forget(metal.regions[oldest].buffer);
		metal.region_count--;
		metal.regions[oldest] = metal.regions[metal.region_count];
		metal.regions[metal.region_count].buffer = nil;
	}
	region = &metal.regions[metal.region_count++];
	region->start = start;
	region->end = extent_end;
	region->buffer = buffer;
	region->used = metal.region_clock;
	guest_recent_add(start, extent_end, buffer, generation);
	pthread_mutex_unlock(&metal.region_lock);
	*offset = address - start;
	return buffer;
}

/* the buffer and offset a source is read from, or nil */
static id<MTLBuffer> source_buffer(struct xgpu_metal_source source, NSUInteger *offset)
{
	id<MTLBuffer> buffer;

	switch (source.kind)
	{
	case XGPU_METAL_SOURCE_RING:
		return ring_buffer(source, offset);
	case XGPU_METAL_SOURCE_STREAM_RING:
		if (!metal.stream_ring || (uint64_t)source.offset + source.size > metal.stream_ring.length)
			return nil;
		*offset = source.offset;
		return metal.stream_ring;
	case XGPU_METAL_SOURCE_BUFFER:
		if (!source.handle || source.handle > metal.buffers.count ||
			(uint64_t)source.offset + source.size > metal.buffers[source.handle - 1].length)
			return nil;
		metal.buffers_used[source.handle - 1] = @(recording_serial());
		*offset = source.offset;
		return metal.buffers[source.handle - 1];
	case XGPU_METAL_SOURCE_GUEST:
		buffer = guest_buffer(source.offset, source.size, offset);
		if (buffer)
			return buffer;
		/* (not mapped as a whole, or not wrapped: copied) */
		statistics_add(&metal.statistics.guest_bytes_copied, source.size);
		return ring_buffer(xgpu_metal_stream((const void *)host_address(source.offset), source.size), offset);
	default:
		return nil;
	}
}

int xgpu_metal_set_stream_ring(void *ring, uint32_t size)
{
	metal.stream_ring = [metal.device newBufferWithBytesNoCopy:ring length:size options:MTLResourceStorageModeShared
		deallocator:nil];
	if (!metal.stream_ring)
		host_logf(HOST_LOG_ERROR, "metal: cannot wrap the command stream's ring (%u bytes at %p)", size, ring);
	return metal.stream_ring != nil;
}

uint32_t xgpu_metal_buffer_create(uint32_t size)
{
	id<MTLBuffer> buffer = [metal.device newBufferWithLength:size options:MTLResourceStorageModeShared];

	if (!buffer)
		return 0;
	[metal.buffers addObject:buffer];
	[metal.buffers_used addObject:@0];
	[metal.buffers_written addObject:@0];
	return (uint32_t)metal.buffers.count;
}

const void *xgpu_metal_buffer_contents(uint32_t buffer)
{
	return buffer && buffer <= metal.buffers.count ? metal.buffers[buffer - 1].contents : NULL;
}

/* a buffer written while the GPU's work still reads it is renamed (below)
up to this size; the buffers it had are kept until that work is done, to
be renamed into again */
#define BUFFER_RENAME_LARGEST (4u << 20)
#define BUFFER_SPARES 8

static id<MTLBuffer> buffer_spare(NSUInteger length)
{
	NSUInteger index;

	for (index = 0; index < metal.buffer_spares.count; index++)
	{
		id<MTLBuffer> spare = metal.buffer_spares[index];

		if (spare.length == length && metal.buffer_spares_serials[index].unsignedLongLongValue <= xgpu_metal_frame_completed())
		{
			[metal.buffer_spares removeObjectAtIndex:index];
			[metal.buffer_spares_serials removeObjectAtIndex:index];
			return spare;
		}
	}
	return [metal.device newBufferWithLength:length options:MTLResourceStorageModeShared];
}

static void buffer_retire(id<MTLBuffer> old, uint64_t used)
{
	if (!metal.buffer_spares)
	{
		metal.buffer_spares = [NSMutableArray new];
		metal.buffer_spares_serials = [NSMutableArray new];
	}
	[metal.buffer_spares addObject:old];
	[metal.buffer_spares_serials addObject:@(used)];
	if (metal.buffer_spares.count > BUFFER_SPARES)
	{
		[metal.buffer_spares removeObjectAtIndex:0];
		[metal.buffer_spares_serials removeObjectAtIndex:0];
	}
}

void xgpu_metal_buffer_write(uint32_t buffer, uint32_t offset, const void *data, uint32_t size, int unused)
{
	id<MTLBuffer> object;
	struct xgpu_metal_source source;
	NSUInteger source_offset;
	id<MTLBuffer> source_buffer_object;
	id<MTLBlitCommandEncoder> blit;

	if (!buffer || buffer > metal.buffers.count || !size)
		return;
	object = metal.buffers[buffer - 1];
	if ((uint64_t)offset + size > object.length)
		return;
	if (unused || metal.buffers_used[buffer - 1].unsignedLongLongValue <= xgpu_metal_frame_completed())
	{
		/* nothing the GPU has to do reads those bytes */
		memcpy((unsigned char *)object.contents + offset, data, size);
		statistics_add(&metal.statistics.uploads_direct, 1);
		return;
	}
	/* (the GPU's writes to it all done: another buffer takes its place,
with its contents and the bytes written, and the work that reads the old
one reads what it had; a blit in the command buffer's order would end
the pass, the screen's multisampled colour, depth and stencil stored and
loaded again for a few kilobytes) */
	if (metal.buffers_written[buffer - 1].unsignedLongLongValue <= xgpu_metal_frame_completed() &&
		object.length <= BUFFER_RENAME_LARGEST)
	{
		id<MTLBuffer> renamed = buffer_spare(object.length);

		if (renamed)
		{
			memcpy(renamed.contents, object.contents, object.length);
			memcpy((unsigned char *)renamed.contents + offset, data, size);
			buffer_retire(object, metal.buffers_used[buffer - 1].unsignedLongLongValue);
			metal.buffers[buffer - 1] = renamed;
			metal.buffers_used[buffer - 1] = @0;
			statistics_add(&metal.statistics.uploads_direct, 1);
			return;
		}
	}
	/* in the command buffer's order: a blit from the ring */
	source = xgpu_metal_stream(data, size);
	source_buffer_object = ring_buffer(source, &source_offset);
	if (!source_buffer_object)
		return;
	pass_end();
	blit = blit_encoder("buffer");
	[blit copyFromBuffer:source_buffer_object sourceOffset:source_offset toBuffer:object destinationOffset:offset size:size];
	[blit endEncoding];
	metal.buffers_used[buffer - 1] = metal.buffers_written[buffer - 1] = @(recording_serial());
	statistics_add(&metal.statistics.uploads_ordered, 1);
}

uint64_t xgpu_metal_fence(void)
{
	if (metal.command_buffer || metal.encoder || metal.color_load == MTLLoadActionClear ||
		metal.depth_load == MTLLoadActionClear || metal.stencil_load == MTLLoadActionClear)
	{
		if (!metal.in_frame)
			xgpu_metal_frame_begin();
		metal.slot->fence = command_buffer_commit(nil);
		/* (the next pass loads what this one stored) */
		metal.dirty = _dirty_all;
	}
	return metal.command_serial;
}

/* ---------- frames */


void xgpu_metal_frame_begin(void)
{
	struct frame_slot *slot;
	uint32_t index;

	if (metal.in_frame)
		return;
	metal.frame_number++;
	slot = &metal.slots[metal.frame_number % FRAME_SLOTS];
	/* (the frame that last had these resources, FRAMES_IN_FLIGHT + 1
	frames ago, is done after the one FRAMES_IN_FLIGHT ago) */
	xgpu_metal_frame_wait(slot->fence);
	slot->chunk = 0;
	slot->position = 0;
	for (index = 0; index < slot->visibility_passes; index++)
	{
		if (slot->visibility_pass_entries[index])
			memset(slot->visibility_buffers[index].contents, 0, slot->visibility_pass_entries[index] * sizeof(uint64_t));
		slot->visibility_pass_entries[index] = 0;
	}
	slot->visibility_passes = 0;
	slot->visibility_entries = 0;
	slot->number = metal.frame_number;
	metal.slot = slot;
	metal.in_frame = YES;
	metal.dirty = _dirty_all;
	profile_frame_begin(metal.frame_number);
	if (metal.frame_number > FRAMES_IN_FLIGHT)
		xgpu_metal_frame_wait(metal.slots[(metal.frame_number - FRAMES_IN_FLIGHT) % FRAME_SLOTS].fence);
}

/* the frame's visibility tests' counts, as its last command buffer
completes */
static void visibility_publish(struct frame_slot *slot, uint32_t entries, uint64_t number)
{
	uint32_t entry;

	pthread_mutex_lock(&visibility.lock);
	for (entry = 0; entry < entries; entry++)
	{
		uint32_t test = slot->visibility_slots[entry];
		uint64_t samples;

		if (visibility.frames[test] != number)
		{
			visibility.frames[test] = number;
			visibility.samples[test] = 0;
		}
		samples = visibility.samples[test] + ((const uint64_t *)
			slot->visibility_buffers[slot->visibility_entry_passes[entry]].contents)[slot->visibility_entry_offsets[entry]];
		visibility.samples[test] = samples < XGPU_METAL_VISIBILITY_MOST ? (uint32_t)samples : XGPU_METAL_VISIBILITY_MOST;
		if (metal.visibility_output)
		{
			metal.visibility_output[test] = visibility.areas[test] > 1.0f ?
				(uint32_t)((float)visibility.samples[test] / visibility.areas[test] + 0.5f) : visibility.samples[test];
			/* (the test's number after its count: xgpu_stream.h) */
			__atomic_store_n(&metal.visibility_output[XGPU_METAL_VISIBILITY_SLOTS + test],
				slot->visibility_numbers[entry], __ATOMIC_RELEASE);
		}
	}
	pthread_mutex_unlock(&visibility.lock);
}

uint64_t xgpu_metal_frame_end(uint32_t present)
{
	struct frame_slot *slot;
	uint32_t entries;
	uint64_t number;
	struct texture_entry *picture = texture_get(present);
	id<MTLTexture> image = nil;
	BOOL copied = NO, timed;

	if (!metal.in_frame)
		xgpu_metal_frame_begin();
	/* (the pipelines made for the draws written once no more have come for
	a while, in the background) */
	if (metal.archive_changed && seconds_now() - metal.archive_changed_at > ARCHIVE_SETTLED_SECONDS)
		archive_save(0.0);
	/* (the frame's last pass: its stores at the frame's end) */
	metal.frame_ending = YES;
	pass_end();
	metal.frame_ending = NO;
	slot = metal.slot;
	entries = slot->visibility_entries;
	number = slot->number;
	if (picture && host_metal_window_active() && (picture->texture.textureType != MTLTextureType2D ||
		picture->texture.mipmapLevelCount != 1))
	{
		/* (a target of levels or layers: its first, copied into an image of
		the presentation's) */
		id<MTLBlitCommandEncoder> blit;

		texture_contents(picture);
		if (!metal.present_copy || metal.present_copy.width != picture->description.width ||
			metal.present_copy.height != picture->description.height)
		{
			MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
				width:picture->description.width height:picture->description.height mipmapped:NO];

			descriptor.usage = MTLTextureUsageShaderRead;
			descriptor.storageMode = MTLStorageModePrivate;
			metal.present_copy = [metal.device newTextureWithDescriptor:descriptor];
		}
		image = metal.present_copy;
		blit = blit_encoder("present");
		[blit copyFromTexture:picture->texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
			sourceSize:MTLSizeMake(picture->description.width, picture->description.height, 1) toTexture:image
			destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
		[blit endEncoding];
		picture->used = recording_serial();
		copied = YES;
	}
	else if (picture && host_metal_window_active())
	{
		/* the picture is the back buffer's texture, given to the
		presentation: the next frame draws into another (the one the
		presentation gives back, no longer shown). A copy of the picture
		into one of the presentation's would be a pass over the whole of
		both. That frame finds the pictures of this one where it does not
		clear the whole of it first (texture_contents) */
		texture_contents(picture);
		image = picture->texture;
		picture->used = recording_serial();
	}
	timed = atomic_load(&profile.state) == 1;
	if (timed)
		atomic_store(&profile.state, 2);
	/* Publish GPU work before waiting for a presentation slot. An aside
	buffer can already hold a queue position ahead of the presentation
	completion that frees that slot. Waiting first would form a cycle. */
	if (image)
		host_metal_present_signal(command_buffer(), number);
	slot->fence = command_buffer_commit(entries || timed ? ^{
		if (entries)
			visibility_publish(slot, entries, number);
		if (timed)
			profile_log();
	} : nil);
	if (image)
	{
		id<MTLTexture> next = host_metal_present_exchange(image);

		if (copied)
			metal.present_copy = next;
		else
		{
			if (!next)
			{
				MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:image.pixelFormat
					width:image.width height:image.height mipmapped:NO];

				descriptor.usage = image.usage;
				descriptor.storageMode = image.storageMode;
				descriptor.swizzle = image.swizzle;
				next = [metal.device newTextureWithDescriptor:descriptor];
			}
			picture->previous = image;
			picture->texture = next;
		}
	}
	if (profile.capturing && number == profile.capture_frame + 1)
	{
		[[MTLCaptureManager sharedCaptureManager] stopCapture];
		profile.capturing = NO;
		host_logf(HOST_LOG_INFO, "metal: captured into %s", profile.capture_path.UTF8String);
	}
	/* (nil: a frame with nothing to show, taken at once) */
	host_metal_present_queue(image, number);
	metal.in_frame = NO;
	statistics_add(&metal.statistics.frames, 1);
	{
		/* (the render thread's CPU time since the last frame's end) */
		static double last;
		struct timespec now;
		double seconds;

		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
		seconds = (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
		pthread_mutex_lock(&metal.statistics_lock);
		metal.statistics.draws += metal.counted.draws;
		metal.statistics.passes += metal.counted.passes;
		metal.statistics.passes_aside += metal.counted.passes_aside;
		memset(&metal.counted, 0, sizeof(metal.counted));
		if (last > 0.0)
			metal.statistics.render_cpu_seconds += seconds - last;
		pthread_mutex_unlock(&metal.statistics_lock);
		last = seconds;
	}
	return slot->fence;
}

uint64_t xgpu_metal_visibility_result(uint32_t slot, uint32_t *samples)
{
	uint64_t frame;

	if (slot >= XGPU_METAL_VISIBILITY_SLOTS)
	{
		*samples = 0;
		return 0;
	}
	pthread_mutex_lock(&visibility.lock);
	*samples = visibility.samples[slot];
	frame = visibility.frames[slot];
	pthread_mutex_unlock(&visibility.lock);
	return frame;
}

/* ---------- passes */

void xgpu_metal_set_targets(uint32_t color, uint32_t color_level, uint32_t color_layer, uint32_t depth,
	int multisampled)
{
	struct texture_entry *color_entry = texture_get(color), *depth_entry = texture_get(depth);

	if (!metal.in_frame)
		xgpu_metal_frame_begin();
	/* (the same targets: the pass goes on, or the clears of whole
	attachments waiting for it to begin still wait: a pass of its own for
	them would store the targets, and the next would load them again) */
	if ((metal.encoder || metal.color_load == MTLLoadActionClear || metal.depth_load == MTLLoadActionClear ||
		metal.stencil_load == MTLLoadActionClear) && metal.color == (color_entry ? color : 0) && metal.color_level == color_level &&
		metal.color_layer == color_layer && metal.depth == (depth_entry ? depth : 0) &&
		metal.multisampled == (multisampled != 0))
		return;
	/* (back to the parked pass's targets: it goes on, except the first time
	in a frame, when it ends there and a pass of its own begins, as it did
	before passes were put aside: with none of those breaks, a frame drawn
	in one pass from its start differs from the frame drawn without aside
	passes at a few pixels of some frames, which the one break keeps
	identical on the recordings (REPORT); or to another target that can be
	put aside too; else what is open ends) */
	if (metal.aside && metal.parked.color == (color_entry ? color : 0) && metal.parked.color_level == color_level &&
		metal.parked.color_layer == color_layer && metal.parked.depth == (depth_entry ? depth : 0) &&
		metal.parked.multisampled == (multisampled != 0))
	{
		if (metal.aside_frame == metal.frame_number || metal.options.aside_continue)
		{
			encoder_end();
			aside_unpark();
			return;
		}
		metal.aside_frame = metal.frame_number;
		aside_close();
	}
	else if (aside_possible(color_entry, color_level, depth_entry != NULL, multisampled))
	{
		if (metal.aside)
			encoder_end();
		else
		{
			metal.parked.encoder = metal.encoder;
			metal.parked.color = metal.color;
			metal.parked.color_level = metal.color_level;
			metal.parked.color_layer = metal.color_layer;
			metal.parked.depth = metal.depth;
			metal.parked.target_width = metal.target_width;
			metal.parked.target_height = metal.target_height;
			metal.parked.multisampled = metal.multisampled;
			metal.parked.visibility_pass = metal.visibility_pass;
			metal.parked.profile_current = profile.current;
			metal.encoder = nil;
			metal.aside = YES;
		}
	}
	else
		pass_end();
	metal.color = color_entry ? color : 0;
	metal.color_level = color_level;
	metal.color_layer = color_layer;
	metal.depth = depth_entry ? depth : 0;
	metal.multisampled = multisampled != 0;
	metal.color_load = MTLLoadActionLoad;
	metal.depth_load = MTLLoadActionLoad;
	metal.stencil_load = MTLLoadActionLoad;
	if (color_entry)
	{
		metal.target_width = color_entry->description.width >> color_level;
		metal.target_height = color_entry->description.height >> color_level;
	}
	else if (depth_entry)
	{
		metal.target_width = depth_entry->description.width;
		metal.target_height = depth_entry->description.height;
	}
	else
	{
		metal.target_width = metal.target_height = 0;
	}
	if (!metal.target_width)
		metal.target_width = 1;
	if (!metal.target_height)
		metal.target_height = 1;
}

static void visibility_entry_begin(void)
{
	struct frame_slot *slot = metal.slot;

	int32_t entry;

	if (slot->visibility_entries >= VISIBILITY_ENTRIES || metal.visibility_pass < 0 ||
		slot->visibility_pass_entries[metal.visibility_pass] >= VISIBILITY_PASS_ENTRIES)
	{
		metal.visibility_entry = -1;
		return;
	}
	entry = (int32_t)slot->visibility_entries++;
	slot->visibility_slots[entry] = 0;
	slot->visibility_numbers[entry] = 0;
	slot->visibility_entry_passes[entry] = (uint16_t)metal.visibility_pass;
	slot->visibility_entry_offsets[entry] = slot->visibility_pass_entries[metal.visibility_pass]++;
	metal.visibility_entry = entry;
	[metal.encoder setVisibilityResultMode:MTLVisibilityResultModeCounting
		offset:(NSUInteger)slot->visibility_entry_offsets[entry] * sizeof(uint64_t)];
}

/* the render command encoder for the targets, begun if there is none */
static id<MTLRenderCommandEncoder> encoder(void)
{
	MTLRenderPassDescriptor *pass;
	struct texture_entry *color = texture_get(metal.color), *depth = texture_get(metal.depth);
	uint64_t serial;

	if (metal.encoder)
		return metal.encoder;
	if (!color && !depth)
		return nil;
	if (!metal.in_frame)
		xgpu_metal_frame_begin();
	/* (a back buffer given to the presentation: its pictures, unless this
	pass clears the whole of it, or resolves into the whole of it) */
	if (color && color->previous)
	{
		if (metal.color_load == MTLLoadActionClear || (color->multisampled && metal.multisampled))
			color->previous = nil;
		else
			texture_contents(color);
	}
	serial = recording_serial();
	/* (an aside pass's target that the command buffer's own work has read:
	another texture, with its contents unless the pass clears them) */
	if (metal.aside && color && color->main_sampled == serial &&
		!aside_rename(color, metal.color_load != MTLLoadActionClear || color->texture.mipmapLevelCount > 1 ||
			color->texture.arrayLength > 1 || color->texture.textureType != MTLTextureType2D))
		aside_close();
	pass = [MTLRenderPassDescriptor renderPassDescriptor];
	if (color)
	{
		MTLRenderPassColorAttachmentDescriptor *attachment = pass.colorAttachments[0];

		if (color->multisampled && metal.multisampled)
		{
			multisampled_current(color, metal.color_load != MTLLoadActionClear);
			if (color->multisampled_memoryless && metal.color_load != MTLLoadActionClear)
			{
				multisampled_demote(color, "a pass loading them");
				metal.color_load = MTLLoadActionDontCare;
			}
			attachment.texture = color->multisampled;
			attachment.resolveTexture = color->texture;
			attachment.resolveLevel = metal.color_level;
			attachment.resolveSlice = metal.color_layer;
			/* (decided as the pass ends: encoder_stores; an aside
			pass is never multisampled) */
			attachment.storeAction = MTLStoreActionUnknown;
			metal.encoder_color_deferred = YES;
		}
		else
		{
			attachment.texture = color->texture;
			attachment.level = metal.color_level;
			attachment.slice = metal.color_layer;
			attachment.storeAction = MTLStoreActionStore;
		}
		attachment.loadAction = metal.color_load;
		attachment.clearColor = metal.clear_color;
		texture_first_use(color, metal.color_load == MTLLoadActionClear);
		color->used = serial;
		if (!metal.aside)
			color->main_written = serial;
	}
	if (depth)
	{
		const struct xgpu_metal_format_information *format = format_get(depth->description.format);
		id<MTLTexture> texture;

		if (depth->multisampled && metal.multisampled)
		{
			multisampled_current(depth, metal.depth_load != MTLLoadActionClear ||
				(format->stencil && metal.stencil_load != MTLLoadActionClear));
			if (depth->multisampled_memoryless && (metal.depth_load != MTLLoadActionClear ||
				(format->stencil && metal.stencil_load != MTLLoadActionClear)))
			{
				multisampled_demote(depth, "a pass loading them");
				metal.depth_load = metal.stencil_load = MTLLoadActionDontCare;
			}
		}
		texture = depth->multisampled && metal.multisampled ? depth->multisampled : depth->texture;

		pass.depthAttachment.texture = texture;
		pass.depthAttachment.loadAction = metal.depth_load;
		pass.depthAttachment.clearDepth = metal.clear_depth;
		/* (decided as the pass ends: encoder_stores; the main command
		buffer's passes alone have depth) */
		pass.depthAttachment.storeAction = metal.aside ? MTLStoreActionStore : MTLStoreActionUnknown;
		metal.encoder_depth_deferred = !metal.aside;
		if (format->stencil)
		{
			pass.stencilAttachment.texture = texture;
			pass.stencilAttachment.loadAction = metal.stencil_load;
			pass.stencilAttachment.clearStencil = metal.clear_stencil;
			pass.stencilAttachment.storeAction = metal.aside ? MTLStoreActionStore : MTLStoreActionUnknown;
			metal.encoder_stencil_deferred = !metal.aside;
		}
		texture_first_use(depth, metal.depth_load == MTLLoadActionClear &&
			(!format->stencil || metal.stencil_load == MTLLoadActionClear));
		depth->used = serial;
		depth->main_written = serial;
	}
	/* (a buffer for each pass: those of earlier passes stay as they are) */
	metal.visibility_pass = -1;
	if (metal.slot->visibility_passes < VISIBILITY_PASSES)
	{
		struct frame_slot *slot = metal.slot;
		uint32_t index = slot->visibility_passes++;

		if (!slot->visibility_buffers[index])
			slot->visibility_buffers[index] = [metal.device newBufferWithLength:VISIBILITY_PASS_ENTRIES * sizeof(uint64_t)
				options:MTLResourceStorageModeShared];
		pass.visibilityResultBuffer = slot->visibility_buffers[index];
		metal.visibility_pass = (int32_t)index;
	}
	profile.current = profile_pass_add(metal.aside ? "aside" : "render");
	if (profile.current >= 0)
	{
		struct profile_pass *timed = &profile.passes[profile.current];

		if (metal.aside)
			timed->kind = PROFILE_KIND_ASIDE;
		MTLRenderPassSampleBufferAttachmentDescriptor *samples = pass.sampleBufferAttachments[0];

		samples.sampleBuffer = profile.samples;
		samples.startOfVertexSampleIndex = (NSUInteger)profile.current * 4;
		samples.endOfVertexSampleIndex = (NSUInteger)profile.current * 4 + 1;
		samples.startOfFragmentSampleIndex = (NSUInteger)profile.current * 4 + 2;
		samples.endOfFragmentSampleIndex = (NSUInteger)profile.current * 4 + 3;
		timed->color = color ? metal.color : 0;
		timed->depth = depth ? metal.depth : 0;
		timed->width = metal.target_width;
		timed->height = metal.target_height;
		timed->color_format = color ? color->description.format : 0;
		timed->depth_format = depth ? depth->description.format : 0;
		timed->samples = metal.multisampled && ((color && color->multisampled) || (depth && depth->multisampled)) ?
			(color && color->multisampled ? color->description.samples : depth->description.samples) : 1;
		if (color)
		{
			timed->loads[0] = (int8_t)pass.colorAttachments[0].loadAction;
			timed->stores[0] = (int8_t)pass.colorAttachments[0].storeAction;
		}
		if (depth)
		{
			timed->loads[1] = (int8_t)pass.depthAttachment.loadAction;
			timed->stores[1] = (int8_t)pass.depthAttachment.storeAction;
			if (pass.stencilAttachment.texture)
			{
				timed->loads[2] = (int8_t)pass.stencilAttachment.loadAction;
				timed->stores[2] = (int8_t)pass.stencilAttachment.storeAction;
			}
		}
	}
	if (!metal.aside)
	{
		metal.encoder_color = color ? metal.color : 0;
		metal.encoder_depth = depth ? metal.depth : 0;
	}
	metal.encoder = [(metal.aside ? aside_buffer() : command_buffer()) renderCommandEncoderWithDescriptor:pass];
	if (metal.aside)
		metal.counted.passes_aside++;
	/* (the clears are done: the pass goes on from what it stores) */
	metal.color_load = metal.depth_load = metal.stencil_load = MTLLoadActionLoad;
	metal.dirty = _dirty_all;
	metal.visibility_pending = metal.visibility_active;
	metal.counted.passes++;
	return metal.encoder;
}

/* ends the pass; one that has not begun is begun first if it clears
something (a clear of whole attachments waits for the pass's start) */
static void pass_end(void)
{
	if (!metal.encoder && (metal.color_load == MTLLoadActionClear || metal.depth_load == MTLLoadActionClear ||
		metal.stencil_load == MTLLoadActionClear))
		encoder();
	if (metal.aside)
		aside_close();
	else
		encoder_end();
}

/* the clear pipeline for the targets and parts */
static id<MTLRenderPipelineState> clear_pipeline(uint32_t parts)
{
	struct texture_entry *color = texture_get(metal.color), *depth = texture_get(metal.depth);
	struct xgpu_metal_clear_key key = { 0 };
	NSData *data;
	id<MTLRenderPipelineState> state;
	MTLRenderPipelineDescriptor *descriptor;
	NSError *error = nil;

	key.color_format = color ? color->description.format : 0;
	key.depth_format = depth ? depth->description.format : 0;
	key.samples = !metal.multisampled ? 1 : color && color->multisampled ? color->description.samples :
		depth && depth->multisampled ? depth->description.samples : 1;
	/* (the colour's channels written) */
	key.parts = (uint8_t)(parts & XGPU_METAL_CLEAR_COLOR ? parts & XGPU_METAL_CLEAR_CHANNELS : 0);
	data = [NSData dataWithBytes:&key length:sizeof(key)];
	state = metal.clear_pipelines[data];
	if (state)
		return state;
	descriptor = xgpu_metal_clear_descriptor(&key);
	descriptor.vertexFunction = metal.clear_vertex;
	descriptor.fragmentFunction = metal.clear_fragment;
	state = [metal.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
	if (!state)
		metal_log(HOST_LOG_ERROR, @"the clear pipeline: %@", error.localizedDescription);
	else
		metal.clear_pipelines[data] = state;
	return state;
}

void xgpu_metal_clear(const int32_t (*rectangles)[4], uint32_t count, uint32_t parts, const float color[4],
	float depth, uint32_t stencil)
{
	struct texture_entry *color_entry = texture_get(metal.color), *depth_entry = texture_get(metal.depth);
	int32_t whole[1][4];
	BOOL has_stencil = depth_entry && format_get(depth_entry->description.format)->stencil;
	id<MTLRenderCommandEncoder> render;
	id<MTLRenderPipelineState> state;
	uint32_t index;

	/* (depth reversed: xgpu_backend.h) */
	depth = 1.0f - depth;
	if (!color_entry || !(parts & XGPU_METAL_CLEAR_CHANNELS))
		parts &= ~(uint32_t)XGPU_METAL_CLEAR_COLOR;
	if (!depth_entry)
		parts &= ~(uint32_t)XGPU_METAL_CLEAR_DEPTH;
	if (!has_stencil)
		parts &= ~(uint32_t)XGPU_METAL_CLEAR_STENCIL;
	if (!(parts & (XGPU_METAL_CLEAR_COLOR | XGPU_METAL_CLEAR_DEPTH | XGPU_METAL_CLEAR_STENCIL)))
		return;
	if (!count)
	{
		whole[0][0] = whole[0][1] = 0;
		whole[0][2] = (int32_t)metal.target_width;
		whole[0][3] = (int32_t)metal.target_height;
		rectangles = (const int32_t (*)[4])whole;
		count = 1;
	}
	/* before the pass begins, a clear of whole attachments is their load
	action (not of some of a colour's channels: drawn) */
	if (!metal.encoder && count == 1 && rectangles[0][0] <= 0 && rectangles[0][1] <= 0 &&
		(!(parts & XGPU_METAL_CLEAR_COLOR) || (parts & XGPU_METAL_CLEAR_CHANNELS) == XGPU_METAL_CLEAR_CHANNELS) &&
		rectangles[0][0] + rectangles[0][2] >= (int32_t)metal.target_width &&
		rectangles[0][1] + rectangles[0][3] >= (int32_t)metal.target_height)
	{
		if (parts & XGPU_METAL_CLEAR_COLOR)
		{
			metal.color_load = MTLLoadActionClear;
			metal.clear_color = MTLClearColorMake(color[0], color[1], color[2], color[3]);
		}
		if (parts & XGPU_METAL_CLEAR_DEPTH)
		{
			metal.depth_load = MTLLoadActionClear;
			metal.clear_depth = depth;
		}
		if (parts & XGPU_METAL_CLEAR_STENCIL)
		{
			metal.stencil_load = MTLLoadActionClear;
			metal.clear_stencil = stencil;
		}
		statistics_add(&metal.statistics.clears_as_loads, 1);
		return;
	}

	/* else a quad for each rectangle */
	render = encoder();
	state = render ? clear_pipeline(parts) : nil;
	if (!state)
		return;
	[render setRenderPipelineState:state];
	[render setDepthStencilState:metal.clear_depth_stencil[((parts & XGPU_METAL_CLEAR_DEPTH) ? 1 : 0) |
		((parts & XGPU_METAL_CLEAR_STENCIL) ? 2 : 0)]];
	[render setStencilReferenceValue:stencil & 0xff];
	[render setCullMode:MTLCullModeNone];
	[render setTriangleFillMode:MTLTriangleFillModeFill];
	[render setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
	[render setDepthClipMode:MTLDepthClipModeClip];
	[render setViewport:(MTLViewport){ 0.0, 0.0, metal.target_width, metal.target_height, 0.0, 1.0 }];
	[render setScissorRect:(MTLScissorRect){ 0, 0, metal.target_width, metal.target_height }];
	if (metal.visibility_active && metal.visibility_entry >= 0)
		[render setVisibilityResultMode:MTLVisibilityResultModeDisabled offset:0];
	for (index = 0; index < count; index++)
	{
		int32_t left = rectangles[index][0] > 0 ? rectangles[index][0] : 0;
		int32_t top = rectangles[index][1] > 0 ? rectangles[index][1] : 0;
		int32_t right = rectangles[index][0] + rectangles[index][2];
		int32_t bottom = rectangles[index][1] + rectangles[index][3];
		struct
		{
			float rectangle[4];
			float color[4];
			float depth;
			float pad[3];
		} parameters;

		if (right > (int32_t)metal.target_width)
			right = (int32_t)metal.target_width;
		if (bottom > (int32_t)metal.target_height)
			bottom = (int32_t)metal.target_height;
		if (right <= left || bottom <= top)
			continue;
		/* (clip space: y up, the targets' rows from the top) */
		parameters.rectangle[0] = 2.0f * (float)left / (float)metal.target_width - 1.0f;
		parameters.rectangle[1] = 1.0f - 2.0f * (float)top / (float)metal.target_height;
		parameters.rectangle[2] = 2.0f * (float)right / (float)metal.target_width - 1.0f;
		parameters.rectangle[3] = 1.0f - 2.0f * (float)bottom / (float)metal.target_height;
		memcpy(parameters.color, color, sizeof(parameters.color));
		parameters.depth = depth;
		[render setVertexBytes:&parameters length:sizeof(parameters) atIndex:0];
		[render setFragmentBytes:&parameters length:sizeof(parameters) atIndex:0];
		[render drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
		statistics_add(&metal.statistics.clears_as_quads, 1);
		if (profile.current >= 0)
			profile.passes[profile.current].clears++;
	}
	if (metal.visibility_active && metal.visibility_entry >= 0)
		[render setVisibilityResultMode:MTLVisibilityResultModeCounting
			offset:(NSUInteger)metal.slot->visibility_entry_offsets[metal.visibility_entry] * sizeof(uint64_t)];
	/* (the draws' state is set again) */
	metal.dirty = _dirty_all;
}

/* ---------- pipelines */

void xgpu_metal_set_pipeline(const struct xgpu_metal_pipeline *pipeline)
{
	if (metal.have_pipeline && !memcmp(&metal.pipeline, pipeline, sizeof(*pipeline)))
		return;
	metal.pipeline = *pipeline;
	metal.have_pipeline = YES;
	metal.dirty |= _dirty_pipeline | _dirty_depth_stencil | _dirty_raster | _dirty_streams;
}

/* work on the making queue once both groups (either NULL) are done */
static void pipeline_after(dispatch_group_t first, dispatch_group_t second, dispatch_block_t work)
{
	if (first)
		dispatch_group_notify(first, metal.making_queue, ^{
			pipeline_after(nil, second, work);
		});
	else if (second)
		dispatch_group_notify(second, metal.making_queue, work);
	else
		dispatch_async(metal.making_queue, work);
}

/* makes a pipeline on the making queue (once: none is made twice at once),
when its shaders are; the draws use it from when it is made (into group,
if not NULL: the loading screen's, or a draw's waiting for it) */
static void pipeline_make_later(const struct xgpu_metal_pipeline_state_key *key, dispatch_group_t group)
{
	struct xgpu_metal_pipeline_state_key copy = *key;
	dispatch_group_t vertex_making, pixel_making, making;
	NSData *data = [NSData dataWithBytes:key length:sizeof(*key)];

	os_unfair_lock_lock(&metal.lock);
	if (metal.pipelines[data] || (metal.pipelines_making[data] && !group))
	{
		os_unfair_lock_unlock(&metal.lock);
		return;
	}
	if ((making = metal.pipelines_making[data]) != nil)
	{
		/* (being made: a group waits for it to be; on the concurrent queue,
		another making of it could find it still being made) */
		os_unfair_lock_unlock(&metal.lock);
		dispatch_group_enter(group);
		dispatch_group_notify(making, metal.making_queue, ^{
			dispatch_group_leave(group);
		});
		return;
	}
	making = dispatch_group_create();
	dispatch_group_enter(making);
	metal.pipelines_making[data] = making;
	vertex_making = key->vertex_shader <= metal.shader_count ? metal.shaders[key->vertex_shader].making : nil;
	pixel_making = key->pixel_shader <= metal.shader_count ? metal.shaders[key->pixel_shader].making : nil;
	os_unfair_lock_unlock(&metal.lock);
	if (group)
		dispatch_group_enter(group);
	/* (made once its shaders are: waited for without a thread held) */
	pipeline_after(vertex_making, pixel_making, ^{
		MTLRenderPipelineDescriptor *descriptor;
		id<MTLRenderPipelineState> state = nil;
		NSError *error = nil;
		double start;

		start = seconds_now();
		descriptor = xgpu_metal_pipeline_descriptor(&copy);
		descriptor.vertexFunction = shader_function(copy.vertex_shader);
		descriptor.fragmentFunction = shader_function(copy.pixel_shader);
		if (descriptor.vertexFunction)
		{
			if (metal.archive)
			{
				descriptor.binaryArchives = @[metal.archive];
				state = [metal.device newRenderPipelineStateWithDescriptor:descriptor
					options:MTLPipelineOptionFailOnBinaryArchiveMiss reflection:nil error:&error];
				if (state)
					statistics_add(&metal.statistics.pipelines_from_archive, 1);
				descriptor.binaryArchives = nil;
				error = nil;
			}
			if (!state)
				state = [metal.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
			if (!state)
				metal_log(HOST_LOG_ERROR, @"cannot make a pipeline: %@", error.localizedDescription);
		}
		os_unfair_lock_lock(&metal.lock);
		[metal.pipelines_making removeObjectForKey:data];
		if (state)
		{
			metal.pipelines[data] = state;
			if (xgpu_metal_pipeline_archivable(&copy))
				[metal.archived addObject:[descriptor copy]];
			metal.archive_changed = YES;
			metal.archive_changed_at = seconds_now();
		}
		os_unfair_lock_unlock(&metal.lock);
		dispatch_group_leave(making);
		pthread_mutex_lock(&metal.statistics_lock);
		metal.statistics.pipelines_made++;
		metal.statistics.pipeline_seconds += seconds_now() - start;
		metal.compile_totals.pipelines++;
		metal.compile_totals.pipeline_seconds += seconds_now() - start;
		pthread_mutex_unlock(&metal.statistics_lock);
		if (group)
			dispatch_group_leave(group);
	});
}

/* a draw's pipeline state: made (by the loading screen's preparation, or
an earlier draw); else asked for on the making queue, and nil: the draw is
skipped until it is made (logged and counted: one the map's preparation did
not make). A pipeline is a compile of tens of milliseconds, of a frame or
more: waited for, a burst of them stopped the frame for 67 to 117 ms on an
M2; skipped, the draws that need it are missing from a frame or two, as
Vulkan's are (xgpu_vulkan_stream.c). The preparation makes every pipeline
the map's draws need (render_enumerate.c), and again as a setting changes
them, so that a draw skipped is the preparation's miss, in the log. With
options.synchronous (the tests'), or before any map's preparation, made and
waited for. During a
preparation (draws unseen), only asked for. Never compiled on this
thread. */
static id<MTLRenderPipelineState> pipeline_state_get(const struct xgpu_metal_pipeline *pipeline)
{
	struct xgpu_metal_pipeline_state_key key;
	NSData *data;
	id<MTLRenderPipelineState> state;
	dispatch_group_t made;

	xgpu_metal_pipeline_state_key_make(pipeline, &key);
	if (state = metal.pipeline_state, state && !memcmp(&key, &metal.pipeline_state_key, sizeof(key)))
		return state;
	data = [NSData dataWithBytes:&key length:sizeof(key)];
	os_unfair_lock_lock(&metal.lock);
	state = metal.pipelines[data];
	os_unfair_lock_unlock(&metal.lock);
	if (!state && metal.preparing_draws)
	{
		pipeline_make_later(&key, metal.preparing);
		return nil;
	}
	if (!state && !metal.options.synchronous && metal.prepared_once)
	{
		BOOL making;

		os_unfair_lock_lock(&metal.lock);
		making = metal.pipelines_making[data] != nil;
		os_unfair_lock_unlock(&metal.lock);
		if (!making)
		{
			statistics_add(&metal.statistics.draws_waiting, 1);
			host_logf(HOST_LOG_WARN, "metal: a pipeline missed (not prepared as the map loaded): made on the "
				"compilers, its draws skipped until it is");
		}
		pipeline_make_later(&key, nil);
		return nil;
	}
	if (!state)
	{
		host_stall_note("metal: a pipeline made and waited for");
		made = dispatch_group_create();
		pipeline_make_later(&key, made);
		dispatch_group_wait(made, DISPATCH_TIME_FOREVER);
		os_unfair_lock_lock(&metal.lock);
		state = metal.pipelines[data];
		os_unfair_lock_unlock(&metal.lock);
	}
	if (state)
		metal.pipeline_state_key = key;
	return state;
}

void xgpu_metal_prepare(const struct xgpu_metal_pipeline *pipeline)
{
	struct xgpu_metal_pipeline_state_key key;

	xgpu_metal_pipeline_state_key_make(pipeline, &key);
	pipeline_make_later(&key, metal.preparing);
}

int xgpu_metal_pipeline_known(const struct xgpu_metal_pipeline *pipeline)
{
	struct xgpu_metal_pipeline_state_key key;
	NSData *data;
	BOOL made;

	xgpu_metal_pipeline_state_key_make(pipeline, &key);
	if (metal.pipeline_state && !memcmp(&key, &metal.pipeline_state_key, sizeof(key)))
		return 1;
	data = [NSData dataWithBytes:&key length:sizeof(key)];
	os_unfair_lock_lock(&metal.lock);
	made = metal.pipelines[data] != nil || metal.pipelines_making[data] != nil;
	os_unfair_lock_unlock(&metal.lock);
	return made;
}

int xgpu_metal_pipeline_made(const struct xgpu_metal_pipeline *pipeline)
{
	struct xgpu_metal_pipeline_state_key key;
	NSData *data;
	BOOL made;

	xgpu_metal_pipeline_state_key_make(pipeline, &key);
	if (metal.pipeline_state && !memcmp(&key, &metal.pipeline_state_key, sizeof(key)))
		return 1;
	data = [NSData dataWithBytes:&key length:sizeof(key)];
	os_unfair_lock_lock(&metal.lock);
	made = metal.pipelines[data] != nil;
	os_unfair_lock_unlock(&metal.lock);
	return made;
}

void xgpu_metal_prepare_begin(void)
{
	metal.preparing_draws = YES;
}

double xgpu_metal_prepared(void)
{
	double start = seconds_now();

	metal.preparing_draws = NO;
	metal.prepared_once = YES;
	host_stall_note("metal: the map's pipelines waited for");
	dispatch_group_wait(metal.preparing, DISPATCH_TIME_FOREVER);
	/* (a safe point: the user's archive written with the map's pipelines,
	in the background) */
	archive_save(0.0);
	return seconds_now() - start;
}

static id<MTLDepthStencilState> depth_stencil_state_get(const struct xgpu_metal_pipeline *pipeline)
{
	struct depth_stencil_key key;
	NSData *data;
	id<MTLDepthStencilState> state;
	MTLDepthStencilDescriptor *descriptor;

	memset(&key, 0, sizeof(key));
	if (pipeline->depth_format)
	{
		key.depth_test = pipeline->depth_test;
		key.depth_function = pipeline->depth_test ? pipeline->depth_function : XGPU_COMPARE_ALWAYS;
		key.depth_write = pipeline->depth_write;
		if (format_get(pipeline->depth_format)->stencil && pipeline->stencil_test)
		{
			key.stencil_test = 1;
			key.stencil_function = pipeline->stencil_function;
			key.stencil_fail = pipeline->stencil_fail;
			key.stencil_depth_fail = pipeline->stencil_depth_fail;
			key.stencil_pass = pipeline->stencil_pass;
			key.stencil_read_mask = pipeline->stencil_read_mask;
			key.stencil_write_mask = pipeline->stencil_write_mask;
		}
	}
	if (metal.depth_stencil_state && !memcmp(&key, &metal.depth_stencil_key, sizeof(key)))
		return metal.depth_stencil_state;
	data = [NSData dataWithBytes:&key length:sizeof(key)];
	state = metal.depth_stencil_states[data];
	if (state)
	{
		metal.depth_stencil_key = key;
		metal.depth_stencil_state = state;
		return state;
	}
	descriptor = [MTLDepthStencilDescriptor new];
	/* (depth reversed: xgpu_backend.h) */
	descriptor.depthCompareFunction = key.depth_test ?
		xgpu_metal_compare_function(xgpu_reversed_depth_function(key.depth_function)) : MTLCompareFunctionAlways;
	descriptor.depthWriteEnabled = key.depth_write != 0;
	if (key.stencil_test)
	{
		MTLStencilDescriptor *stencil = [MTLStencilDescriptor new];

		stencil.stencilCompareFunction = xgpu_metal_compare_function(key.stencil_function);
		stencil.stencilFailureOperation = xgpu_metal_stencil_operation(key.stencil_fail);
		stencil.depthFailureOperation = xgpu_metal_stencil_operation(key.stencil_depth_fail);
		stencil.depthStencilPassOperation = xgpu_metal_stencil_operation(key.stencil_pass);
		stencil.readMask = key.stencil_read_mask;
		stencil.writeMask = key.stencil_write_mask;
		descriptor.frontFaceStencil = stencil;
		descriptor.backFaceStencil = stencil;
	}
	state = [metal.device newDepthStencilStateWithDescriptor:descriptor];
	metal.depth_stencil_states[data] = state;
	statistics_add(&metal.statistics.depth_stencil_states_made, 1);
	metal.depth_stencil_key = key;
	metal.depth_stencil_state = state;
	return state;
}

void xgpu_metal_set_dynamic_state(const struct xgpu_metal_dynamic_state *state)
{
	if (memcmp(metal.dynamic.viewport, state->viewport, sizeof(state->viewport)) ||
		memcmp(metal.dynamic.depth_range, state->depth_range, sizeof(state->depth_range)))
		metal.dirty |= _dirty_viewport;
	if (memcmp(metal.dynamic.scissor, state->scissor, sizeof(state->scissor)))
		metal.dirty |= _dirty_scissor;
	if (metal.dynamic.stencil_reference != state->stencil_reference)
		metal.dirty |= _dirty_stencil_reference;
	if (memcmp(metal.dynamic.blend_color, state->blend_color, sizeof(state->blend_color)))
		metal.dirty |= _dirty_blend_color;
	if (memcmp(metal.dynamic.depth_bias, state->depth_bias, sizeof(state->depth_bias)))
		metal.dirty |= _dirty_raster;
	metal.dynamic = *state;
}

/* ---------- bindings */

static id<MTLSamplerState> sampler_get(const struct xgpu_metal_sampler *sampler)
{
	static const MTLSamplerAddressMode address_modes[5] = {
		MTLSamplerAddressModeRepeat, MTLSamplerAddressModeMirrorRepeat, MTLSamplerAddressModeClampToEdge,
		MTLSamplerAddressModeClampToBorderColor, MTLSamplerAddressModeMirrorClampToEdge,
	};
	static const MTLSamplerBorderColor border_colors[3] = {
		MTLSamplerBorderColorTransparentBlack, MTLSamplerBorderColorOpaqueBlack, MTLSamplerBorderColorOpaqueWhite,
	};
	NSData *data = [NSData dataWithBytes:sampler length:sizeof(*sampler)];
	id<MTLSamplerState> state = metal.samplers[data];
	MTLSamplerDescriptor *descriptor;

	if (state)
		return state;
	descriptor = [MTLSamplerDescriptor new];
	descriptor.minFilter = sampler->min_filter ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
	descriptor.magFilter = sampler->mag_filter ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
	descriptor.mipFilter = sampler->mip_filter == XGPU_METAL_MIP_LINEAR ? MTLSamplerMipFilterLinear :
		sampler->mip_filter == XGPU_METAL_MIP_POINT ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterNotMipmapped;
	descriptor.sAddressMode = address_modes[sampler->address_u < 5 ? sampler->address_u : 0];
	descriptor.tAddressMode = address_modes[sampler->address_v < 5 ? sampler->address_v : 0];
	descriptor.rAddressMode = address_modes[sampler->address_w < 5 ? sampler->address_w : 0];
	descriptor.borderColor = border_colors[sampler->border < 3 ? sampler->border : 0];
	descriptor.maxAnisotropy = sampler->max_anisotropy > 1 ? (sampler->max_anisotropy > 16 ? 16 : sampler->max_anisotropy) : 1;
	descriptor.lodMinClamp = sampler->min_lod;
	descriptor.lodMaxClamp = sampler->max_lod > 0.0f ? sampler->max_lod : FLT_MAX;
	if (sampler->compare)
		descriptor.compareFunction = xgpu_metal_compare_function(sampler->compare_function);
	descriptor.supportArgumentBuffers = NO;
	state = [metal.device newSamplerStateWithDescriptor:descriptor];
	metal.samplers[data] = state;
	statistics_add(&metal.statistics.samplers_made, 1);
	return state;
}

void xgpu_metal_set_texture(uint32_t stage, uint32_t texture, const struct xgpu_metal_sampler *sampler)
{
	if (stage >= XGPU_METAL_TEXTURE_STAGE_COUNT)
		return;
	if (metal.stage_textures[stage] != texture)
	{
		texture_contents(texture_get(texture));
		metal.stage_textures[stage] = texture;
		metal.dirty |= _dirty_textures;
	}
	if (sampler && (!metal.stage_sampler_states[stage] || memcmp(&metal.stage_samplers[stage], sampler, sizeof(*sampler))))
	{
		metal.stage_samplers[stage] = *sampler;
		metal.stage_sampler_states[stage] = sampler_get(sampler);
		metal.dirty |= _dirty_textures;
	}
}

void xgpu_metal_set_vertex_constants(uint32_t first, uint32_t count, const float *values)
{
	if (first >= XGPU_METAL_VERTEX_CONSTANT_COUNT)
		return;
	if (count > XGPU_METAL_VERTEX_CONSTANT_COUNT - first)
		count = XGPU_METAL_VERTEX_CONSTANT_COUNT - first;
	memcpy(metal.vertex_constants[first], values, count * 4 * sizeof(float));
	metal.dirty |= _dirty_vertex_constants;
}

void xgpu_metal_set_draw_uniforms(const void *data, uint32_t size)
{
	if (size > sizeof(metal.draw_uniforms))
		size = sizeof(metal.draw_uniforms);
	if (size == metal.draw_uniforms_size && !memcmp(metal.draw_uniforms, data, size))
		return;
	memcpy(metal.draw_uniforms, data, size);
	metal.draw_uniforms_size = size;
	metal.dirty |= _dirty_draw_uniforms;
}

void xgpu_metal_set_stream(uint32_t stream, struct xgpu_metal_source source)
{
	if (stream >= XGPU_METAL_STREAM_COUNT)
		return;
	metal.streams[stream] = source;
	metal.dirty |= _dirty_streams;
}

void xgpu_metal_set_dynamic_lights(struct xgpu_metal_source block, struct xgpu_metal_source cells)
{
	metal.dynamic_lights[0] = block;
	metal.dynamic_lights[1] = cells;
	metal.dirty |= _dirty_dynamic_lights;
}

void xgpu_metal_set_dynamic_light_world(struct xgpu_metal_source world)
{
	metal.dynamic_lights[2] = world;
	metal.dirty |= _dirty_dynamic_lights;
}

void xgpu_metal_set_world_ao(uint32_t field, uint32_t table, uint32_t scenery, uint32_t objects,
	struct xgpu_metal_source block)
{
	if (metal.world_ao_objects != objects)
		texture_contents(texture_get(objects));
	metal.world_ao_objects = objects;
	if (metal.world_ao_field != field)
		texture_contents(texture_get(field));
	if (metal.world_ao_table != table)
		texture_contents(texture_get(table));
	if (metal.world_ao_scenery != scenery)
		texture_contents(texture_get(scenery));
	metal.world_ao_field = field;
	metal.world_ao_table = table;
	metal.world_ao_scenery = scenery;
	metal.world_ao_block = block;
	metal.dirty |= _dirty_world_ao;
}

void xgpu_metal_set_world_ao_pixels(uint32_t texture)
{
	metal.world_ao_pixels = texture;
	metal.dirty |= _dirty_world_ao;
}

void xgpu_metal_set_constant_attributes(const float (*values)[4])
{
	if (!memcmp(metal.constant_attributes, values, sizeof(metal.constant_attributes)))
		return;
	memcpy(metal.constant_attributes, values, sizeof(metal.constant_attributes));
	metal.dirty |= _dirty_constant_attributes;
}

/* ---------- draws */

static const MTLPrimitiveType primitive_types[5] = {
	MTLPrimitiveTypePoint, MTLPrimitiveTypeLine, MTLPrimitiveTypeLineStrip, MTLPrimitiveTypeTriangle,
	MTLPrimitiveTypeTriangleStrip,
};

/* sets on the encoder what the draw needs of the state; NO if it cannot
be drawn */
static BOOL draw_state_apply(id<MTLRenderCommandEncoder> render)
{
	uint32_t dirty = metal.dirty;
	uint64_t serial = metal.command_serial;
	int index;

	if (dirty & _dirty_pipeline)
	{
		id<MTLRenderPipelineState> state = pipeline_state_get(&metal.pipeline);

		if (!state)
			return NO;
		metal.pipeline_state = state;
		[render setRenderPipelineState:state];
	}
	if (dirty & _dirty_depth_stencil)
		[render setDepthStencilState:depth_stencil_state_get(&metal.pipeline)];
	if (dirty & _dirty_raster)
	{
		const struct xgpu_metal_pipeline *pipeline = &metal.pipeline;

		[render setCullMode:!pipeline->cull ? MTLCullModeNone : pipeline->cull_front ? MTLCullModeFront : MTLCullModeBack];
		[render setFrontFacingWinding:pipeline->front_counter_clockwise ? MTLWindingCounterClockwise : MTLWindingClockwise];
		/* (Metal draws no points for a polygon: lines) */
		[render setTriangleFillMode:pipeline->fill == XGPU_FILL_SOLID ? MTLTriangleFillModeFill : MTLTriangleFillModeLines];
		/* (on reversed depth: the constant bias the shader's, the slope's
		turned: xgpu_backend.h) */
		if (pipeline->depth_bias)
			[render setDepthBias:0.0f slopeScale:-metal.dynamic.depth_bias[0] clamp:0.0f];
		else
			[render setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
		[render setDepthClipMode:pipeline->depth_clamp ? MTLDepthClipModeClamp : MTLDepthClipModeClip];
	}
	if (dirty & _dirty_viewport)
	{
		const int32_t *viewport = metal.dynamic.viewport;

		/* (the depth range reversed: xgpu_backend.h) */
		[render setViewport:(MTLViewport){ viewport[0], viewport[1], viewport[2], viewport[3],
			1.0 - metal.dynamic.depth_range[1], 1.0 - metal.dynamic.depth_range[0] }];
	}
	if (dirty & _dirty_scissor)
	{
		/* (inside the targets, as Metal requires) */
		int32_t left = metal.dynamic.scissor[0] > 0 ? metal.dynamic.scissor[0] : 0;
		int32_t top = metal.dynamic.scissor[1] > 0 ? metal.dynamic.scissor[1] : 0;
		int32_t right = metal.dynamic.scissor[0] + metal.dynamic.scissor[2];
		int32_t bottom = metal.dynamic.scissor[1] + metal.dynamic.scissor[3];

		if (!metal.dynamic.scissor[2] && !metal.dynamic.scissor[3])
		{
			right = (int32_t)metal.target_width;
			bottom = (int32_t)metal.target_height;
		}
		if (right > (int32_t)metal.target_width)
			right = (int32_t)metal.target_width;
		if (bottom > (int32_t)metal.target_height)
			bottom = (int32_t)metal.target_height;
		if (left > right)
			left = right;
		if (top > bottom)
			top = bottom;
		[render setScissorRect:(MTLScissorRect){ (NSUInteger)left, (NSUInteger)top, (NSUInteger)(right - left),
			(NSUInteger)(bottom - top) }];
	}
	if (dirty & _dirty_stencil_reference)
		[render setStencilReferenceValue:metal.dynamic.stencil_reference & 0xff];
	if (dirty & _dirty_blend_color)
		[render setBlendColorRed:metal.dynamic.blend_color[0] green:metal.dynamic.blend_color[1]
			blue:metal.dynamic.blend_color[2] alpha:metal.dynamic.blend_color[3]];
	if (dirty & _dirty_vertex_constants)
		[render setVertexBytes:metal.vertex_constants length:sizeof(metal.vertex_constants)
			atIndex:XGPU_METAL_BUFFER_VERTEX_CONSTANTS];
	if (dirty & _dirty_constant_attributes)
		[render setVertexBytes:metal.constant_attributes length:sizeof(metal.constant_attributes)
			atIndex:XGPU_METAL_BUFFER_CONSTANT_ATTRIBUTES];
	if ((dirty & _dirty_draw_uniforms) && metal.draw_uniforms_size)
	{
		[render setVertexBytes:metal.draw_uniforms length:metal.draw_uniforms_size atIndex:XGPU_METAL_BUFFER_DRAW_UNIFORMS];
		[render setFragmentBytes:metal.draw_uniforms length:metal.draw_uniforms_size atIndex:XGPU_METAL_BUFFER_DRAW_UNIFORMS];
	}
	if (dirty & _dirty_textures)
	{
		for (index = 0; index < XGPU_METAL_TEXTURE_STAGE_COUNT; index++)
		{
			struct texture_entry *entry = texture_get(metal.stage_textures[index]);

			[render setFragmentTexture:entry ? entry->texture : nil atIndex:XGPU_METAL_TEXTURE_STAGE(index)];
			/* (and at its type's place, as the combiner's function reads it:
			xgpu_metal_combiner.h) */
			if (entry && entry->texture.textureType != MTLTextureType2D)
			{
				NSUInteger place = entry->texture.textureType == MTLTextureType3D ? XGPU_METAL_COMBINER_TEXTURE_3D :
					entry->texture.textureType == MTLTextureTypeCube ? XGPU_METAL_COMBINER_TEXTURE_CUBE :
					XGPU_METAL_COMBINER_TEXTURE_2D_ARRAY;

				[render setFragmentTexture:entry->texture atIndex:place + (NSUInteger)index];
			}
			if (metal.stage_sampler_states[index])
				[render setFragmentSamplerState:metal.stage_sampler_states[index] atIndex:XGPU_METAL_SAMPLER_STAGE(index)];
		}
	}
	/* (each draw: the textures it reads are in use until its command buffer
	is done) */
	for (index = 0; index < XGPU_METAL_TEXTURE_STAGE_COUNT; index++)
	{
		struct texture_entry *entry = texture_get(metal.stage_textures[index]);

		if (entry)
		{
			entry->used = serial;
			if (!metal.aside)
				entry->main_sampled = serial;
			texture_first_use(entry, NO);
		}
	}
	if (dirty & _dirty_streams)
	{
		uint32_t used = 0;

		for (index = 0; index < 16; index++)
		{
			if (metal.pipeline.attributes[index].format != XGPU_METAL_ATTRIBUTE_NONE &&
				metal.pipeline.attributes[index].stream < XGPU_METAL_STREAM_COUNT)
				used |= 1u << metal.pipeline.attributes[index].stream;
		}
		for (index = 0; index < XGPU_METAL_STREAM_COUNT; index++)
		{
			NSUInteger offset = 0;
			id<MTLBuffer> buffer;

			if (!(used & (1u << index)))
				continue;
			buffer = source_buffer(metal.streams[index], &offset);
			if (!buffer)
				return NO;
			[render setVertexBuffer:buffer offset:offset atIndex:XGPU_METAL_BUFFER_STREAM(index)];
		}
	}
	if (dirty & _dirty_dynamic_lights)
	{
		static const NSUInteger places[3] = { XGPU_METAL_BUFFER_DYNAMIC_LIGHTS, XGPU_METAL_BUFFER_DYNAMIC_LIGHT_CELLS,
			XGPU_METAL_BUFFER_DYNAMIC_LIGHT_WORLD };

		for (index = 0; index < 3; index++)
		{
			NSUInteger offset = 0;
			id<MTLBuffer> buffer = metal.dynamic_lights[index].kind == XGPU_METAL_SOURCE_NONE ? nil :
				source_buffer(metal.dynamic_lights[index], &offset);

			if (buffer)
				[render setFragmentBuffer:buffer offset:offset atIndex:places[index]];
		}
	}
	if (dirty & _dirty_world_ao)
	{
		struct texture_entry *entry = texture_get(metal.world_ao_field);
		struct texture_entry *table = texture_get(metal.world_ao_table);
		struct texture_entry *scenery = texture_get(metal.world_ao_scenery);
		struct texture_entry *objects = texture_get(metal.world_ao_objects);
		NSUInteger offset = 0;
		id<MTLBuffer> buffer = metal.world_ao_block.kind == XGPU_METAL_SOURCE_NONE ? nil :
			source_buffer(metal.world_ao_block, &offset);

		if (entry)
		{
			[render setVertexTexture:entry->texture atIndex:XGPU_METAL_TEXTURE_WORLD_SDF];
			[render setFragmentTexture:entry->texture atIndex:XGPU_METAL_TEXTURE_WORLD_SDF];
		}
		if (table)
		{
			[render setVertexTexture:table->texture atIndex:XGPU_METAL_TEXTURE_WORLD_SDF_TABLE];
			[render setFragmentTexture:table->texture atIndex:XGPU_METAL_TEXTURE_WORLD_SDF_TABLE];
		}
		if (scenery)
			[render setFragmentTexture:scenery->texture atIndex:XGPU_METAL_TEXTURE_WORLD_AO_SCENERY];
		if (objects)
			[render setFragmentTexture:objects->texture atIndex:XGPU_METAL_TEXTURE_WORLD_AO_OBJECTS];
		if (texture_get(metal.world_ao_pixels))
			[render setFragmentTexture:texture_get(metal.world_ao_pixels)->texture
				atIndex:XGPU_METAL_TEXTURE_WORLD_AO_PIXELS];
		if (buffer)
		{
			[render setVertexBuffer:buffer offset:offset atIndex:XGPU_METAL_BUFFER_WORLD_AO];
			[render setFragmentBuffer:buffer offset:offset atIndex:XGPU_METAL_BUFFER_WORLD_AO];
		}
	}
	/* (the field too: in use until the draw's command buffer is done) */
	{
		uint32_t const fields[5] = { metal.world_ao_field, metal.world_ao_table, metal.world_ao_scenery,
			metal.world_ao_objects, metal.world_ao_pixels };
		int part;

		for (part = 0; part < 5; part++)
		{
			struct texture_entry *entry = texture_get(fields[part]);

			if (entry)
			{
				entry->used = serial;
				if (!metal.aside)
					entry->main_sampled = serial;
				texture_first_use(entry, NO);
			}
		}
	}
	metal.dirty = 0;
	return YES;
}

/* (HALO_METAL_PASSES_SPLIT) */
static void profile_draw_describe(struct profile_pass *pass, uint32_t primitive, uint32_t count)
{
	size_t used;
	int index;
	id<MTLFunction> vertex = shader_function(metal.pipeline.vertex_shader);
	id<MTLFunction> pixel = shader_function(metal.pipeline.pixel_shader);

	used = (size_t)snprintf(pass->detail, sizeof(pass->detail),
		"%s %s prim %u x%u blend %u(%u,%u) mask %x depth %u/%u stencil %u viewport %dx%d scissor %dx%d",
		vertex ? vertex.name.UTF8String : "-", pixel ? pixel.name.UTF8String : "-", primitive, count,
		metal.pipeline.blend, metal.pipeline.blend_source, metal.pipeline.blend_destination, metal.pipeline.color_mask,
		metal.pipeline.depth_test, metal.pipeline.depth_write, metal.pipeline.stencil_test, metal.dynamic.viewport[2],
		metal.dynamic.viewport[3], metal.dynamic.scissor[2], metal.dynamic.scissor[3]);
	for (index = 0; index < XGPU_METAL_TEXTURE_STAGE_COUNT && used < sizeof(pass->detail); index++)
	{
		struct texture_entry *entry = texture_get(metal.stage_textures[index]);
		const struct xgpu_metal_sampler *sampler = &metal.stage_samplers[index];

		if (!entry)
			continue;
		used += (size_t)snprintf(pass->detail + used, sizeof(pass->detail) - used, " t%d %ux%u f%u l%u (%u%u%u a%u)",
			index, entry->description.width, entry->description.height, entry->description.format,
			(unsigned)entry->texture.mipmapLevelCount, sampler->min_filter, sampler->mag_filter, sampler->mip_filter,
			sampler->max_anisotropy);
	}
}

static xgpu_metal_draw_inspector draw_inspector;

void xgpu_metal_command_buffer_finish(void)
{
	pass_end();
	xgpu_metal_frame_wait(command_buffer_commit(nil));
	metal.dirty = _dirty_all;
}

void xgpu_metal_set_draw_inspector(xgpu_metal_draw_inspector inspector)
{
	draw_inspector = inspector;
}

void xgpu_metal_draw(uint32_t primitive, uint32_t count, uint32_t first_vertex, int32_t base_vertex,
	struct xgpu_metal_source indices)
{
	static BOOL refused;
	id<MTLRenderCommandEncoder> render;

	if (!count || !metal.have_pipeline)
		return;
	if (primitive >= sizeof(primitive_types) / sizeof(primitive_types[0]))
	{
		if (!refused)
			host_logf(HOST_LOG_ERROR, "metal: primitive %u has no Metal equivalent (the front end converts fans and loops)",
				primitive);
		refused = YES;
		return;
	}
	if (metal.aside && aside_reads_written(indices))
		aside_close();
	if (draw_inspector)
	{
		pass_end();
		metal.dirty = _dirty_all;
		draw_inspector(0, metal.color, metal.color_level);
	}
	render = encoder();
	if (!render || !draw_state_apply(render))
	{
		/* (the state not applied is applied at the next draw) */
		metal.dirty |= _dirty_all;
		return;
	}
	/* (a visibility test's entry in this pass, its counting begun once the
	draw's state is set) */
	if (metal.visibility_pending)
	{
		visibility_entry_begin();
		metal.visibility_pending = NO;
	}
	if (indices.kind != XGPU_METAL_SOURCE_NONE)
	{
		NSUInteger offset = 0;
		id<MTLBuffer> buffer = source_buffer(indices, &offset);

		if (!buffer)
			return;
		[render drawIndexedPrimitives:primitive_types[primitive] indexCount:count indexType:MTLIndexTypeUInt16
			indexBuffer:buffer indexBufferOffset:offset instanceCount:1 baseVertex:base_vertex baseInstance:0];
	}
	else
	{
		[render drawPrimitives:primitive_types[primitive] vertexStart:first_vertex vertexCount:count];
	}
	metal.counted.draws++;
	if (draw_inspector)
	{
		pass_end();
		metal.dirty = _dirty_all;
		draw_inspector(1, metal.color, metal.color_level);
	}
	if (profile.current >= 0)
	{
		if (profile.timing && profile.timing_draw != UINT32_MAX)
		{
			/* (each draw's pass, numbered as the caller numbers them) */
			if (profile.timing_draw >= profile.timing_draw_capacity)
			{
				uint32_t capacity = profile.timing_draw_capacity ? profile.timing_draw_capacity : 1024;
				uint32_t *grown;

				while (capacity <= profile.timing_draw)
					capacity *= 2;
				grown = realloc(profile.timing_draw_pass, capacity * sizeof(*grown));
				if (grown)
				{
					profile.timing_draw_pass = grown;
					profile.timing_draw_capacity = capacity;
				}
			}
			if (profile.timing_draw < profile.timing_draw_capacity)
			{
				while (profile.timing_draws <= profile.timing_draw)
					profile.timing_draw_pass[profile.timing_draws++] = UINT32_MAX;
				profile.timing_draw_pass[profile.timing_draw] = (uint32_t)profile.current;
			}
		}
		profile.passes[profile.current].draws++;
		profile.passes[profile.current].kinds |= 1u << metal.pass_kind;
		if (profile.split)
		{
			profile_draw_describe(&profile.passes[profile.current], primitive, count);
			encoder_end();
			metal.dirty = _dirty_all;
		}
	}
}

uint32_t xgpu_metal_pass_kind(uint32_t kind)
{
	uint32_t was = metal.pass_kind;

	if (kind >= XGPU_METAL_PASS_KINDS || kind == was)
		return was;
	metal.pass_kind = kind;
	/* (HALO_METAL_PASS_TIMES_SPLIT: a timed frame's pass ends here, the
	next draw beginning another of this kind) */
	if (profile.kind_split && profile.current >= 0 && metal.encoder && !metal.aside &&
		profile.passes[profile.current].draws)
	{
		encoder_end();
		metal.dirty = _dirty_all;
	}
	return was;
}

/* ---------- visibility tests */

void xgpu_metal_visibility_begin(void)
{
	if (metal.visibility_active)
		xgpu_metal_visibility_end(0, 1.0f, 0);
	metal.visibility_active = YES;
	metal.visibility_entry = -1;
	metal.visibility_first = metal.slot->visibility_entries;
	/* (each pass counts into an entry of its own, begun at the pass's
	first draw of the test) */
	metal.visibility_pending = YES;
}

void xgpu_metal_visibility_end(uint32_t slot, float area, uint32_t number)
{
	uint32_t entry;

	if (!metal.visibility_active)
		return;
	if (slot < XGPU_METAL_VISIBILITY_SLOTS)
	{
		pthread_mutex_lock(&visibility.lock);
		visibility.areas[slot] = area;
		pthread_mutex_unlock(&visibility.lock);
	}
	if (metal.encoder && metal.visibility_entry >= 0)
		[metal.encoder setVisibilityResultMode:MTLVisibilityResultModeDisabled offset:0];
	for (entry = metal.visibility_first; entry < metal.slot->visibility_entries; entry++)
	{
		metal.slot->visibility_slots[entry] = (uint16_t)(slot < XGPU_METAL_VISIBILITY_SLOTS ? slot : 0);
		metal.slot->visibility_numbers[entry] = number;
	}
	metal.visibility_active = NO;
	metal.visibility_entry = -1;
	metal.visibility_pending = NO;
}

void xgpu_metal_set_visibility_output(volatile uint32_t *counts)
{
	pthread_mutex_lock(&visibility.lock);
	metal.visibility_output = counts;
	pthread_mutex_unlock(&visibility.lock);
}

/* ---------- reading pixels */

int xgpu_metal_read_pixels(uint32_t texture, uint32_t level, uint32_t layer, uint32_t x, uint32_t y,
	uint32_t width, uint32_t height, void *pixels, uint32_t bytes_per_row)
{
	struct texture_entry *entry = texture_get(texture);
	const struct xgpu_metal_format_information *format;
	id<MTLBuffer> buffer;
	id<MTLBlitCommandEncoder> blit;
	uint32_t row_size, row;
	uint64_t serial;
	BOOL depth;

	if (!entry || !width || !height)
		return 0;
	texture_contents(entry);
	format = format_get(entry->description.format);
	if (format->compressed)
		return 0;
	depth = format->depth;
	/* (depth as 4-byte floats: the depth plane of D24S8; D16 as it is) */
	row_size = width * (depth && format->stencil ? 4 : format->bytes);
	/* (the buffer and the command buffer let go of at once: a reading each
	frame, as debug.determinism_log's is, would otherwise keep a frame's
	pixels each until the render thread's pool drains) */
	@autoreleasepool
	{
		buffer = [metal.device newBufferWithLength:(NSUInteger)row_size * height options:MTLResourceStorageModeShared];
		pass_end();
		blit = blit_encoder("read");
		[blit copyFromTexture:entry->texture sourceSlice:layer sourceLevel:level sourceOrigin:MTLOriginMake(x, y, 0)
			sourceSize:MTLSizeMake(width, height, 1) toBuffer:buffer destinationOffset:0
			destinationBytesPerRow:row_size destinationBytesPerImage:(NSUInteger)row_size * height
			options:depth && format->stencil ? MTLBlitOptionDepthFromDepthStencil : MTLBlitOptionNone];
		[blit endEncoding];
		entry->used = recording_serial();
		/* (the frame goes on in a command buffer of its own) */
		serial = command_buffer_commit(nil);
		xgpu_metal_frame_wait(serial);
		for (row = 0; row < height; row++)
		{
			memcpy((unsigned char *)pixels + (size_t)row * bytes_per_row,
				(const unsigned char *)buffer.contents + (size_t)row * row_size, row_size);
			/* (float depth as the game has it: reversed, xgpu_backend.h) */
			if (depth && row_size == width * 4)
			{
				float *values = (float *)((unsigned char *)pixels + (size_t)row * bytes_per_row);
				uint32_t column;

				for (column = 0; column < width; column++)
					values[column] = 1.0f - values[column];
			}
		}
		buffer = nil;
	}
	metal.dirty = _dirty_all;
	return 1;
}
