/*
HOST_METAL.M

The game's window on macOS, drawn with Metal: its layer, the Metal backend
(xgpu_metal.m) set up for it, and the presentation of frames, paced by the
display.

The guest makes the window with SDL_WINDOW_METAL (sdl_platform.c) and hands
it over (host_metal_window_open, on the main thread, which Cocoa's views
need): SDL makes a view with a CAMetalLayer, whose drawables are the
window's pixels (Retina's included; resized with the window, and in
fullscreen).

Presentation runs on a thread of its own, driven by a CAMetalDisplayLink on
its run loop: at each of the display's refreshes the link hands over a
drawable, and the thread draws the oldest frame queued (letterboxed to the
frame's shape) into it and presents it. A ProMotion display's rate follows
the frames, up to its highest (120 Hz); a fixed one's is its own. A frame's
picture becomes one of two presentation images when the frame ends
(host_metal_present_exchange): the back buffer's texture itself, whose
place the image it replaces takes for the next frame (xgpu_metal.m).

The game is paced by the display (frame_pacing.h, in the command stream's
host): each refresh takes the oldest frame queued TAKE_MARGIN_MS
before the link's deadline for it (its update's targetTimestamp), not as
the link calls, a refresh earlier. The deadline is the refresh's latch,
with the refreshes' period (the least interval between the link's
targets): the game starts each frame its budget before the latch it is
for, with at most one frame waiting. The budget is the most of the last
frames' costs, from their input read to their GPU work done
(host_metal_present_signal's host_render_stream_frame_finished: the
presentation waits for that work), and a margin the stream learns from the
frames a take found missing (host_render_stream_frame_displayed). Each
frame's time on screen goes back with its input time, for the input to
screen statistics.

With display.vsync off (host_metal_vsync, the Mac's default), the game is
not paced by the display: it draws as many frames as display.max_fps lets
it (sdl_platform.c's platform_video_swap, twice the display's rate by
default, a press starting the next frame at once), and each refresh shows
the newest frame queued, the older one dropped. The link still presents
only at the refreshes, so nothing tears, in a window or fullscreen. The
newest frame is taken just before the link's deadline for the refresh (its
update's targetTimestamp, TAKE_MARGIN_MS before it), not as the
link calls: the link calls a refresh before that deadline (on an M2 at
60 Hz in a window: the deadline 16.6 ms after the call, the frame on
screen 49.9 ms after it), so the frame shown is a refresh newer. The game
starts its frames at fixed phases of those takes (host_metal_frame_phase),
one its budget before each, so that the frame each refresh shows is always
as old, and the game's time on screen steps evenly.

Until frames come (the null renderer draws none), each refresh clears the
drawable to black and presents it.

Battery saver (display.battery_saver, port/linux/src/battery_saver.c:
host_metal_battery_saver) runs the link at a whole fraction of the display's
rate, 30 to 60 frames a second (frame_pacing.h's frame_pacing_divisor: 60
at 60 Hz, and on a 120 Hz ProMotion display, whose refreshes then slow to
it): the game is paced by the link's slower refreshes as it is by the
display's own.

display.hdr (port/linux/src/xgpu_hdr.h): while the frame is drawn in HDR on a
display with EDR headroom, the layer is half floats in extended linear sRGB
with extended dynamic range content, 1.0 the system's SDR white, and the
presentation encodes the frame for it (xgpu_hdr.c's encoding: linear, at
most the peak, times paper white); else it is BGRA8, as without HDR. The
window's screen's headroom is read on the main thread, every quarter second
and as the window moves to another screen: its potential headroom says
whether the display shows HDR (display.hdr "auto"), its headroom now is the
peak the tone map and the encoding roll off to (it changes with the
display's brightness). HALO_METAL_EDR_HEADROOM=<headroom> stands in for the
display's (both), for tests and for the replay, which has no window.

display.gamma: into a BGRA8 layer, where the setting is not 1, the
presentation draws the frame with its curve (each channel raised to one
over the setting: xgpu_metal_post_shaders.h's) in place of the plain copy,
so that it costs no pass of its own; the loading screen's colours are
curved alike. An EDR layer is never curved: paper white is its brightness.

HALO_METAL_STATS=<seconds> logs the pacing that often: the link's
refreshes, the frames presented, the intervals between the times the
drawables reached the screen.

The iOS app (tools/ios_build.py) builds this file too: its window is
UIKit's, its screen a UIScreen (whose EDR headroom is the HDR's), and its
layer fills the screen (the safe areas are the game's to keep its HUD
from: port/ios/README.md).
*/

#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

/* (host.h's host_page_size is Mach's too: not used here) */
#define host_page_size host_page_size_of_guest_memory
#include "host.h"
#include "host_load_trace.h"
#undef host_page_size
#include "xgpu_metal_internal.h"
#include "xgpu_metal_pipeline.h"
#include "host_render_record.h"
#include "host_render_stream.h"
#include "xgpu_metal_stream.h"
#include "xgpu_metal_textures.h"
#include "loading_art.h"
#include "host_loading_capture.h"
#include "xgpu_metal_post_shaders.h"
#include "frame_pacing.h"
#define XGPU_HDR_REFERENCE
#include "xgpu_hdr.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <math.h>
#include <pthread.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>

static void drawn_size(int *width, int *height);

#define PRESENT_IMAGES 2
#define LOADING_UPLOADS 3
/* the longest a frame waits for an image before the oldest queued frame is
dropped (34 ms: two refreshes at 60 Hz; 100 ms froze an iPhone's game, which
runs on the main thread, for a fifth of a second each time its link paused) */
#define PRESENT_WAIT_MAXIMUM_NS 34000000L
/* how long before the link's deadline for a refresh its frame is taken
(the newest with vsync off, the oldest queued with it on): the
presentation's GPU work is about 0.1 ms, and waits for the frame's own if
it is still drawing. Measured on an M2 at 60 Hz in a window, vsync off:
2 to 6 ms missed no refresh (the link's deadline has room), and the
frames' input to screen (median) fell from 75 ms, taken as the link calls,
to 36 to 43 ms. */
#define TAKE_MARGIN_MS 3.0
/* (vsync off) the most the frames' start moves a refresh as their budget
changes (host_metal_frame_phase): the budget, the most of the last frames'
costs, moved by up to 8 ms at once as a long frame came or left */
#define UNPACED_PHASE_SLEW_NS 500000ull

enum
{
	_image_free,
	_image_queued,
	/* drawn into a drawable, until that command buffer is done */
	_image_showing,
};

static struct
{
	SDL_Window *window;
	SDL_MetalView view;
	CAMetalLayer *layer;
	CAMetalDisplayLink *link;
	/* the link's last targets' intervals: its refreshes' period is the
	least of them (a ProMotion display's rate is the link's) */
	double target_intervals[32];
	uint32_t target_interval_next;
	/* the process's activity: latency critical, not napped */
	id<NSObject> activity;
	id<MTLRenderPipelineState> present_pipeline;
	id<MTLRenderPipelineState> loading_pipeline;
	/* display.hdr: the presentation's encoding for an EDR layer (half
	floats); the front end's state (drawn in HDR, the mode, paper white in
	nits, or 0 and its percent of the system's white; the UI's white in nits,
	or 0); the window's screen's potential headroom and headroom now (a
	float's bits); the layer is EDR's; HALO_METAL_EDR_HEADROOM's (0: none) */
	id<MTLRenderPipelineState> edr_pipeline, loading_edr_pipeline;
	/* display.gamma: the presentation with its curve, into an SDR layer
	(nil: none), and the setting (a float's bits; 0 or 1: none) */
	id<MTLRenderPipelineState> gamma_pipeline;
	_Atomic uint32_t gamma;
	_Atomic int hdr_active, hdr_mode, hdr_paper_white_nits, hdr_paper_white, hdr_ui_nits;
	_Atomic uint32_t edr_potential, edr_current;
	BOOL layer_edr;
	uint32_t edr_ticks;
	float edr_override;
	/* All presentation is ordered here, independently of future game work. */
	id<MTLCommandQueue> command_queue;
	id<MTLEvent> render_event;
	struct loading_presentation_frame loading;
	struct loading_art loading_art;
	id<MTLTexture> loading_texture;
	id<MTLBuffer> loading_uploads[LOADING_UPLOADS];
	BOOL loading_upload_busy[LOADING_UPLOADS];
	BOOL loading_texture_valid;
	id<MTLSamplerState> present_sampler;
	_Atomic int active;
	/* the window's size in pixels, as SDL last reported it */
	_Atomic uint32_t pixel_width, pixel_height;
	uint32_t drawable_width, drawable_height;
	float refresh_rate;

	pthread_mutex_t lock;
	pthread_cond_t freed;
	id<MTLTexture> images[PRESENT_IMAGES];
	int image_states[PRESENT_IMAGES];
	/* queued images, oldest first */
	int queue[PRESENT_IMAGES];
	int queued;
	BOOL frames_seen;
	/* a frame waits for an image (host_metal_present_exchange) */
	_Atomic int image_waiting;
	BOOL unpaced;
	/* HALO_METAL_UNPACED: unpaced whatever display.vsync says */
	BOOL unpaced_forced;
	/* battery saver, as the guest asks (host_metal_battery_saver), and as the
	link's rate was last set for it (the link's thread's) */
	_Atomic int battery_saver;
	int battery_saver_set;
	/* (an iPhone) the link at half the display's rate while the frames do
	not keep up with its full rate (rate_adapt, the link's thread's) */
	int rate_halved;
	uint32_t adapt_refreshes, adapt_repeats, adapt_good_windows;
	double adapt_since, adapt_hold, adapt_hold_until;
	/* the window went to a display of another rate (display_rate_read):
	the link's rate is set again at its next refresh */
	_Atomic int rate_changed;
	/* (unpaced) the newest frame is taken this long before the link's
	deadline (seconds: TAKE_MARGIN_MS, HALO_METAL_TAKE_MARGIN_MS),
	and the latest a wait for it ended since the last log (microseconds) */
	double take_margin;
	_Atomic uint64_t take_late_most_us;
	/* (unpaced) the start of the frame the last refresh's take shows on
	the game's clock (CLOCK_MONOTONIC nanoseconds), and the refreshes'
	period (host_metal_frame_phase); the frames' budget it is started
	before the take (the link's thread's) */
	_Atomic uint64_t take_phase_ns, take_period_ns, take_budget_published_ns;
	uint64_t take_budget_ns;
	/* (unpaced) the last frames' times from their input read to queued for
	the display, microseconds (take_budget) */
	_Atomic uint32_t take_costs_us[FRAME_PACING_COSTS];
	_Atomic uint32_t take_cost_next;
	/* (iOS) the app is in the background: no GPU work, which iOS refuses
	there (host_metal_background) */
	_Atomic int background;

	/* pacing, since the last log */
	double stats_interval, stats_since, started;
	uint32_t ticks, frames_presented, idle_presented, skipped, waits, dropped;
	/* the refreshes without a new frame by what the render thread was doing:
	waiting for the GPU to finish a frame, for an image, playing the stream,
	or waiting for the game's */
	uint32_t skipped_gpu, skipped_image, skipped_playing, skipped_game;
	_Atomic uint32_t shown, late, intervals;
	/* (HALO_METAL_STATS) intervals over HITCH_SECONDS; the shaders and
	pipelines made by the time each queued image's frame ended, and by the
	time the frame shown last did */
	_Atomic uint32_t hitches;
	struct xgpu_metal_compile_totals compiled[PRESENT_IMAGES], compiled_before;
	_Atomic uint64_t interval_sum_us, interval_minimum_us, interval_maximum_us;
	/* the presentations' GPU time, their wait from commit to the GPU's
	start, and those shown after the refresh the link drew them for */
	_Atomic uint64_t present_gpu_ns, present_queued_ns, present_queued_most_ns;
	_Atomic uint32_t present_buffers, missed;
	/* presentations whose GPU work ended after their target time less a
	millisecond (the display's latch), and the least slack */
	_Atomic uint32_t present_gpu_late;
	_Atomic int64_t present_slack_least_us;
	double last_presented;
	/* the link's callbacks more than 1.5 refreshes after the last: with
	every drawable still to reach the screen, with some free, and those
	right after another such */
	double last_tick, last_target;
	/* of those, the ones whose target time skipped a refresh too (the link's
	choice), and those for the next refresh (delivered late) */
	uint32_t gaps_target_skipped, gaps_delivered_late;
	uint32_t gaps_starved, gaps_free, gaps_following, gaps_after_slow;
	/* the last callback's time from its start to its return, and the most */
	double last_callback_seconds, callback_most_seconds;
	BOOL last_gap;
	_Atomic int drawables_pending;
	/* each queued image's frame's input time (host_render_stream_frame_input) */
	uint64_t frame_input[PRESENT_IMAGES];
	uint64_t frame_ready[PRESENT_IMAGES];
	/* (paced) the game's frames' GPU work done, on the game's clock, by
	their number (host_metal_present_signal), for the take that finds
	them (host_render_stream_frame_displayed) */
	_Atomic uint64_t frame_finished_ns[8], frame_finished_number[8];
} presentation = {
	.lock = PTHREAD_MUTEX_INITIALIZER,
	.freed = PTHREAD_COND_INITIALIZER,
};

/* ---------- display.hdr */

static float bits_float(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static uint32_t float_bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

/* (the main thread) the window's screen's headroom, for the presentation and
the guest (host_render_stream_display_hdr) */
static void display_hdr_read(void)
{
	float potential = 1.0f, current = 1.0f;

	if (presentation.edr_override > 0.0f)
	{
		potential = current = presentation.edr_override;
	}
	else if (presentation.window)
	{
#if TARGET_OS_IPHONE
		UIWindow *native = (__bridge UIWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(presentation.window),
			SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
		UIScreen *screen = native.windowScene.screen ? native.windowScene.screen : UIScreen.mainScreen;

		potential = (float)screen.potentialEDRHeadroom;
		current = (float)screen.currentEDRHeadroom;
#else
		NSWindow *native = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(presentation.window),
			SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
		NSScreen *screen = native.screen ? native.screen : NSScreen.mainScreen;

		potential = (float)screen.maximumPotentialExtendedDynamicRangeColorComponentValue;
		current = (float)screen.maximumExtendedDynamicRangeColorComponentValue;
#endif
	}
	if (!(potential >= 1.0f))
		potential = 1.0f;
	if (!(current >= 1.0f))
		current = 1.0f;
	if (bits_float(atomic_load(&presentation.edr_potential)) != potential)
		host_logf(HOST_LOG_INFO, "metal: the display's EDR headroom: %.2f potential, %.2f now", potential, current);
	atomic_store(&presentation.edr_potential, float_bits(potential));
	atomic_store(&presentation.edr_current, float_bits(current));
	/* (the guest shows and logs the display's peak: its potential headroom,
	which the headroom now, the tone map's, rises toward as EDR content is
	shown; macOS and iOS give no nits: EDR's 1.0 taken as
	XGPU_HDR_EDR_WHITE_NITS) */
	host_render_stream_display_hdr(potential > XGPU_HDR_DISPLAY_HEADROOM, potential, XGPU_HDR_EDR_WHITE_NITS, 0);
}

void host_metal_hdr_override(float headroom)
{
	presentation.edr_override = headroom;
	atomic_store(&presentation.edr_potential, float_bits(headroom > 1.0f ? headroom : 1.0f));
	atomic_store(&presentation.edr_current, float_bits(headroom > 1.0f ? headroom : 1.0f));
	host_render_stream_display_hdr(headroom > XGPU_HDR_DISPLAY_HEADROOM, headroom > 1.0f ? headroom : 1.0f,
		XGPU_HDR_EDR_WHITE_NITS, 0);
}

void host_metal_hdr_state(int active, int mode, int paper_white_nits, int paper_white, int ui_nits)
{
	atomic_store(&presentation.hdr_active, active);
	atomic_store(&presentation.hdr_mode, mode);
	atomic_store(&presentation.hdr_paper_white_nits, paper_white_nits);
	atomic_store(&presentation.hdr_paper_white, paper_white);
	atomic_store(&presentation.hdr_ui_nits, ui_nits > 0 ? ui_nits : 0);
}

/* the layer is EDR's while the frame is drawn in HDR and the display has
headroom at all (an SDR panel's dimmed white has some: display.hdr "on") */
static BOOL edr_wanted(void)
{
	return atomic_load(&presentation.hdr_active) && bits_float(atomic_load(&presentation.edr_potential)) > 1.0f;
}

void host_metal_hdr_frame(struct xgpu_hdr_frame *frame)
{
	float current = bits_float(atomic_load(&presentation.edr_current));
	/* (paper white over the system's SDR white, EDR's 1.0) */
	float paper_white = xgpu_hdr_paper_white_nits(atomic_load(&presentation.hdr_paper_white_nits),
		atomic_load(&presentation.hdr_paper_white), XGPU_HDR_EDR_WHITE_NITS) / XGPU_HDR_EDR_WHITE_NITS;

	if (presentation.edr_override > 0.0f && !presentation.window)
		current = presentation.edr_override;
	memset(frame, 0, sizeof(*frame));
	frame->mode = atomic_load(&presentation.hdr_mode);
	frame->output = XGPU_HDR_OUTPUT_SDR;
	frame->paper_white = 1.0f;
	frame->peak = 1.0f;
	frame->ui = 1.0f;
	if (!(paper_white > 0.0f))
		paper_white = 1.0f;
	/* (the replay's, which has no window, as its layer would be) */
	if (edr_wanted() || (presentation.edr_override > 0.0f && !presentation.window && atomic_load(&presentation.hdr_active)))
	{
		/* (paper white no brighter than the display can show) */
		if (paper_white > current)
			paper_white = current;
		frame->output = XGPU_HDR_OUTPUT_EXTENDED_LINEAR;
		frame->paper_white = paper_white;
		frame->peak = current / paper_white;
		/* (the UI's white, no brighter than the display either) */
		frame->ui = xgpu_hdr_ui_white((float)atomic_load(&presentation.hdr_ui_nits) / XGPU_HDR_EDR_WHITE_NITS,
			paper_white, current);
	}
}

void host_metal_gamma(float gamma)
{
	atomic_store(&presentation.gamma, float_bits(gamma));
}

/* display.gamma's power for an SDR drawable (one over the setting), or 0
where the setting is 1: none, the presentation's own pipeline */
static float gamma_power(void)
{
	float gamma = bits_float(atomic_load(&presentation.gamma));

	return gamma > 0.0f && gamma != 1.0f && presentation.gamma_pipeline ? 1.0f / gamma : 0.0f;
}

/* a colour's channel (the frame's values) as an EDR drawable takes it:
linear, times the UI's white; as it is for an SDR one, curved by
display.gamma as the frame is */
static float edr_value(id<CAMetalDrawable> drawable, float value)
{
	struct xgpu_hdr_frame frame;
	float power;

	if (drawable.texture.pixelFormat != MTLPixelFormatRGBA16Float)
		return (power = gamma_power()) > 0.0f && value > 0.0f ? powf(value, power) : value;
	host_metal_hdr_frame(&frame);
	return xgpu_hdr_linear(value) * (frame.output == XGPU_HDR_OUTPUT_SDR ? 1.0f : frame.paper_white * frame.ui);
}

/* (the presentation thread, before it draws into the drawable) the layer
made EDR's or SDR's as the frame wants: the next drawables are */
static void layer_hdr_choose(void)
{
	BOOL wanted = edr_wanted();
	CGColorSpaceRef space;

	if (wanted == presentation.layer_edr)
		return;
	presentation.layer_edr = wanted;
	space = wanted ? CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB) : NULL;
	presentation.layer.wantsExtendedDynamicRangeContent = wanted;
	presentation.layer.pixelFormat = wanted ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm;
	presentation.layer.colorspace = space;
	if (space)
		CGColorSpaceRelease(space);
	host_logf(HOST_LOG_INFO, "metal: the window's layer: %s", wanted ? "EDR (half floats, extended linear sRGB)" : "SDR");
}

/* a post function (xgpu_metal_post_shaders.h), the app's library's, or
compiled from its source */
static id<MTLFunction> post_function(int which)
{
	const struct xgpu_metal_post_function *function = &xgpu_metal_post_functions[which];
	NSString *name = @(function->entry);
	id<MTLFunction> found = [xgpu_metal_library() newFunctionWithName:name];
	MTLCompileOptions *options;
	id<MTLLibrary> library;
	NSError *error = nil;

	if (found)
		return found;
	options = [MTLCompileOptions new];
	if (@available(macOS 15.0, iOS 18.0, *))
		options.mathMode = MTLMathModeSafe;
	else
		options.fastMathEnabled = NO;
	library = [xgpu_metal_device() newLibraryWithSource:[[NSString alloc] initWithBytes:function->source
		length:function->size encoding:NSUTF8StringEncoding] options:options error:&error];
	if (!library)
		host_logf(HOST_LOG_ERROR, "metal: %s: %s", function->entry, error.localizedDescription.UTF8String);
	return [library newFunctionWithName:name];
}

int host_metal_window_active(void)
{
	return atomic_load(&presentation.active);
}

void host_metal_loading_art(void *context, const uint8_t *rgba)
{
	(void)context;
	pthread_mutex_lock(&presentation.lock);
	loading_art_set(&presentation.loading_art, rgba);
	presentation.loading_texture_valid = NO;
	pthread_mutex_unlock(&presentation.lock);
}

void host_metal_loading_progress(void *context, float progress)
{
	(void)context;
	pthread_mutex_lock(&presentation.lock);
	loading_art_progress(&presentation.loading_art, progress);
	pthread_mutex_unlock(&presentation.lock);
}

void host_metal_loading_scene(void *context, const void *data, uint32_t bytes)
{
	(void)context;
	pthread_mutex_lock(&presentation.lock);
	loading_art_scene(&presentation.loading_art, bytes ? data : NULL, bytes);
	presentation.loading_texture_valid = NO;
	pthread_mutex_unlock(&presentation.lock);
}

static void loading_upload_initialize(void)
{
	MTLTextureDescriptor *description;
	NSUInteger size = LOADING_ART_FRAME_MAXIMUM_PIXELS * 4;
	int index;

	description = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
		width:LOADING_ART_FRAME_WIDTH height:LOADING_ART_FRAME_HEIGHT mipmapped:NO];
	description.storageMode = MTLStorageModePrivate;
	description.usage = MTLTextureUsageShaderRead;
	presentation.loading_texture = [xgpu_metal_device() newTextureWithDescriptor:description];
	for (index = 0; index < LOADING_UPLOADS; index++)
		presentation.loading_uploads[index] = [xgpu_metal_device() newBufferWithLength:size
			options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
}

static id<MTLTexture> loading_upload(id<MTLCommandBuffer> buffer,
	const struct loading_presentation_frame *frame, NSUInteger screen_width, NSUInteger screen_height)
{
	id<MTLTexture> texture = nil;
	id<MTLBuffer> upload = nil;
	id<MTLBlitCommandEncoder> blit;
	const uint8_t *pixels;
	unsigned int width;
	int index;

	pthread_mutex_lock(&presentation.lock);
	/* (the frame at the screen's shape: a texture of its width) */
	loading_art_width(&presentation.loading_art, (unsigned int)screen_width, (unsigned int)screen_height);
	width = presentation.loading_art.width;
	if (presentation.loading_texture && presentation.loading_texture.width != width)
	{
		MTLTextureDescriptor *description = [MTLTextureDescriptor
			texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:width height:LOADING_ART_FRAME_HEIGHT
			mipmapped:NO];

		description.storageMode = MTLStorageModePrivate;
		description.usage = MTLTextureUsageShaderRead;
		presentation.loading_texture = [xgpu_metal_device() newTextureWithDescriptor:description];
		presentation.loading_texture_valid = NO;
	}
	for (index = 0; index < LOADING_UPLOADS; index++)
	{
		if (presentation.loading_uploads[index] && !presentation.loading_upload_busy[index])
			break;
	}
	if (presentation.loading_texture && index < LOADING_UPLOADS)
	{
		pixels = loading_art_render(&presentation.loading_art, frame);
		if (pixels && host_loading_capture_due(frame))
			host_loading_capture_write(frame, pixels, width, LOADING_ART_FRAME_HEIGHT, 0, 0);
		if (pixels)
		{
			upload = presentation.loading_uploads[index];
			memcpy(upload.contents, pixels, (size_t)width * LOADING_ART_FRAME_HEIGHT * 4);
			presentation.loading_upload_busy[index] = YES;
			presentation.loading_texture_valid = YES;
		}
		else
		{
			presentation.loading_texture_valid = NO;
		}
	}
	if (presentation.loading_texture_valid)
		texture = presentation.loading_texture;
	pthread_mutex_unlock(&presentation.lock);
	if (!upload)
		return texture;
	/* Uploads and all sampling share the presentation queue. A later game
	aside cannot delay them, and CPU writes never reuse an in-flight buffer. */
	blit = [buffer blitCommandEncoder];
	[blit copyFromBuffer:upload sourceOffset:0 sourceBytesPerRow:width * 4
		sourceBytesPerImage:0 sourceSize:MTLSizeMake(width, LOADING_ART_FRAME_HEIGHT, 1)
		toTexture:texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
	[blit endEncoding];
	[buffer addCompletedHandler:^(id<MTLCommandBuffer> done) {
		(void)done;
		pthread_mutex_lock(&presentation.lock);
		presentation.loading_upload_busy[index] = NO;
		pthread_mutex_unlock(&presentation.lock);
	}];
	return texture;
}

int host_metal_loading_owned(void *context)
{
	int owned;

	(void)context;
	pthread_mutex_lock(&presentation.lock);
	owned = atomic_load(&presentation.active) && presentation.loading_pipeline &&
		presentation.loading.active && !presentation.loading.ready;
	pthread_mutex_unlock(&presentation.lock);
	return owned;
}

uint64_t host_metal_loading(void *context, const struct loading_presentation_frame *frame)
{
	uint64_t serial;
	int discarded = 0;

	(void)context;
	pthread_mutex_lock(&presentation.lock);
	if (frame->ready && !presentation.loading.ready)
	{
		/* Only images queued after readiness can be the first game picture. */
		while (presentation.queued)
		{
			presentation.image_states[presentation.queue[--presentation.queued]] = _image_free;
			discarded++;
		}
		pthread_cond_broadcast(&presentation.freed);
	}
	serial = presentation.loading.epoch == frame->epoch ? presentation.loading.serial : 0;
	presentation.loading = *frame;
	presentation.loading.serial = serial;
	pthread_mutex_unlock(&presentation.lock);
	while (discarded-- > 0)
		host_render_stream_frame_taken();
	return presentation.refresh_rate > 0.0f ? (uint64_t)(1e9 / presentation.refresh_rate) : 16666667ull;
}

/* ---------- the presentation images */

id<MTLTexture> host_metal_present_exchange(id<MTLTexture> picture)
{
	int index, found = -1;
	id<MTLTexture> replaced;
	BOOL dropped = NO;

	pthread_mutex_lock(&presentation.lock);
	for (;;)
	{
		struct timespec until;

		for (index = 0; index < PRESENT_IMAGES && found < 0; index++)
		{
			if (presentation.image_states[index] == _image_free)
				found = index;
		}
		if (found >= 0)
			break;
		/* (HALO_METAL_UNPACED: the oldest queued is dropped at once, so that
		the game draws as fast as the GPU and the render thread can: a
		measure of their work at their full speed) */
		if (presentation.unpaced && presentation.queued)
		{
			found = presentation.queue[0];
			memmove(presentation.queue, presentation.queue + 1, sizeof(int) * (size_t)(presentation.queued - 1));
			presentation.queued--;
			presentation.dropped++;
			dropped = YES;
			break;
		}
		presentation.waits++;
		atomic_store(&presentation.image_waiting, 1);
		clock_gettime(CLOCK_REALTIME, &until);
		until.tv_nsec += PRESENT_WAIT_MAXIMUM_NS;
		if (until.tv_nsec >= 1000000000L)
		{
			until.tv_sec++;
			until.tv_nsec -= 1000000000L;
		}
		if (pthread_cond_timedwait(&presentation.freed, &presentation.lock, &until) == ETIMEDOUT &&
			presentation.queued)
		{
			/* the display takes no frames (the window minimized, covered,
			or between the desktop and fullscreen): the oldest queued is
			dropped, so that the game goes on */
			found = presentation.queue[0];
			memmove(presentation.queue, presentation.queue + 1, sizeof(int) * (size_t)(presentation.queued - 1));
			presentation.queued--;
			presentation.dropped++;
			dropped = YES;
			break;
		}
	}
	atomic_store(&presentation.image_waiting, 0);
	replaced = presentation.images[found];
	presentation.images[found] = picture;
	presentation.image_states[found] = _image_queued;
	pthread_mutex_unlock(&presentation.lock);
	/* (a frame dropped is taken: the game's pacing, frame_pacing.h) */
	if (dropped)
		host_render_stream_frame_taken();
	return replaced && replaced.width == picture.width && replaced.height == picture.height &&
		replaced.pixelFormat == picture.pixelFormat && replaced.usage == picture.usage &&
		replaced.storageMode == picture.storageMode ? replaced : nil;
}

void host_metal_present_queue(id<MTLTexture> image, uint64_t frame)
{
	int index;
	BOOL queued = NO;

	pthread_mutex_lock(&presentation.lock);
	for (index = 0; image && index < PRESENT_IMAGES; index++)
	{
		if (presentation.images[index] == image && presentation.queued < PRESENT_IMAGES)
		{
			presentation.queue[presentation.queued++] = index;
			presentation.frame_input[index] = host_render_stream_frame_input();
			if (presentation.unpaced && presentation.frame_input[index])
			{
				struct timespec monotonic;
				uint64_t now;

				clock_gettime(CLOCK_MONOTONIC, &monotonic);
				now = (uint64_t)monotonic.tv_sec * 1000000000ull + (uint64_t)monotonic.tv_nsec;
				if (now > presentation.frame_input[index] && now - presentation.frame_input[index] < 1000000000ull)
					atomic_store(&presentation.take_costs_us[atomic_fetch_add(&presentation.take_cost_next, 1) %
						FRAME_PACING_COSTS], (uint32_t)((now - presentation.frame_input[index]) / 1000u));
			}
			presentation.frame_ready[index] = frame;
			queued = YES;
			if (presentation.stats_interval > 0.0)
				xgpu_metal_compile_totals(&presentation.compiled[index]);
			presentation.frames_seen = YES;
			break;
		}
	}
	pthread_mutex_unlock(&presentation.lock);
	/* (a frame with nothing to show is taken at once) */
	if (!queued)
		host_render_stream_frame_taken();
}

static void image_release(int index)
{
	pthread_mutex_lock(&presentation.lock);
	presentation.image_states[index] = _image_free;
	pthread_cond_broadcast(&presentation.freed);
	pthread_mutex_unlock(&presentation.lock);
}

int host_metal_present_initialize(void)
{
	if (presentation.command_queue && presentation.render_event)
		return 1;
	presentation.command_queue = [xgpu_metal_device() newCommandQueue];
	presentation.render_event = [xgpu_metal_device() newEvent];
	if (!presentation.command_queue || !presentation.render_event)
	{
		host_logf(HOST_LOG_ERROR, "metal: could not create the presentation queue and event");
		return 0;
	}
	return 1;
}

/* The renderer signals before committing its frame, then publishes the image.
No presentation wait can therefore depend on a future uncommitted frame. */
void host_metal_present_signal(id<MTLCommandBuffer> buffer, uint64_t frame)
{
	uint64_t input = host_render_stream_frame_input();

	[buffer encodeSignalEvent:presentation.render_event value:frame];
	/* (the frame's cost, its GPU's work included: the paced game's budget,
	host_render_stream_frame_finished) */
	[buffer addCompletedHandler:^(id<MTLCommandBuffer> done) {
		struct timespec monotonic;
		uint64_t finished;

		if (!(done.GPUEndTime > 0.0))
			return;
		clock_gettime(CLOCK_MONOTONIC, &monotonic);
		finished = (uint64_t)((done.GPUEndTime + (double)monotonic.tv_sec + monotonic.tv_nsec / 1e9 - CACurrentMediaTime()) * 1e9);
		atomic_store(&presentation.frame_finished_ns[frame % 8], finished);
		atomic_store(&presentation.frame_finished_number[frame % 8], frame);
		if (input && !presentation.unpaced)
			host_render_stream_frame_finished(input, finished);
	}];
}

id<MTLCommandBuffer> host_metal_present_buffer(uint64_t producer_frame)
{
	id<MTLCommandBuffer> buffer = [presentation.command_queue commandBuffer];

	if (producer_frame)
		[buffer encodeWaitForEvent:presentation.render_event value:producer_frame];
	return buffer;
}

/* ---------- pacing */

static double seconds_now(void)
{
	return CACurrentMediaTime();
}

/* a frame on screen this long after the last: a hitch, which
HALO_METAL_STATS logs with the shaders and pipelines made since the last
(two refreshes at 60 Hz, a frame missed, are 33.3 ms) */
#define HITCH_SECONDS 0.033

static void presented(double when, double target, const struct xgpu_metal_compile_totals *compiled)
{
	double previous = presentation.last_presented;

	if (when <= 0.0)
		return;
	/* (a refresh's tolerance: the times are the display's) */
	if (target > 0.0 && when > target + 0.001)
		atomic_fetch_add(&presentation.missed, 1);
	presentation.last_presented = when;
	atomic_fetch_add(&presentation.shown, 1);
	if (previous > 0.0)
	{
		uint64_t interval = (uint64_t)((when - previous) * 1e6);
		uint64_t minimum = atomic_load(&presentation.interval_minimum_us);

		atomic_fetch_add(&presentation.interval_sum_us, interval);
		atomic_fetch_add(&presentation.intervals, 1);
		if (!minimum || interval < minimum)
			atomic_store(&presentation.interval_minimum_us, interval);
		if (interval > atomic_load(&presentation.interval_maximum_us))
			atomic_store(&presentation.interval_maximum_us, interval);
		if (presentation.refresh_rate > 0.0f && (double)interval > 1.5e6 / presentation.refresh_rate)
			atomic_fetch_add(&presentation.late, 1);
		if (presentation.stats_interval > 0.0 && when - previous > HITCH_SECONDS)
		{
			atomic_fetch_add(&presentation.hitches, 1);
			if (compiled)
			{
				const struct xgpu_metal_compile_totals *now = compiled, *before = &presentation.compiled_before;
				host_logf(HOST_LOG_INFO, "metal: hitch of %.1f ms at %.2f s: %u shaders compiled (%.1f ms), %u pipelines made "
					"(%.1f ms) by the frame since the frame before", (when - previous) * 1000.0,
					when - presentation.started, now->shaders - before->shaders,
					(now->shader_seconds - before->shader_seconds) * 1000.0, now->pipelines - before->pipelines,
					(now->pipeline_seconds - before->pipeline_seconds) * 1000.0);
			}
		}
	}
	if (compiled)
		presentation.compiled_before = *compiled;
}

/* the device's thermal state, in the statistics: a phone that is hot runs
the game's threads slower, on fewer of its fast cores */
static const char *thermal_state(void)
{
	switch (NSProcessInfo.processInfo.thermalState)
	{
	case NSProcessInfoThermalStateNominal:
		return "nominal";
	case NSProcessInfoThermalStateFair:
		return "fair";
	case NSProcessInfoThermalStateSerious:
		return "serious";
	case NSProcessInfoThermalStateCritical:
		return "critical";
	default:
		return "unknown";
	}
}

static void pacing_log(double now)
{
	double seconds = now - presentation.stats_since;
	uint32_t shown = atomic_exchange(&presentation.shown, 0);
	uint64_t sum = atomic_exchange(&presentation.interval_sum_us, 0);
	uint32_t intervals = atomic_exchange(&presentation.intervals, 0);
	struct xgpu_metal_statistics statistics;
	struct xgpu_metal_texture_statistics textures;
	const struct xgpu_stream_ring *ring = host_render_stream_ring();
	static uint32_t waits_before, wait_nanoseconds_before;
	uint32_t waits = ring ? ring->waits : 0, wait_nanoseconds = ring ? ring->wait_nanoseconds : 0;
	uint32_t buffers = atomic_exchange(&presentation.present_buffers, 0);

	xgpu_metal_statistics(&statistics);
	xgpu_metal_textures_statistics(&textures);
	{
		float median, high;
		uint32_t frames = host_render_stream_latency(&median, &high);

		if (frames)
			host_logf(HOST_LOG_INFO,
				"metal: input to screen (the frame's input read to the frame on screen): %.1f ms median, %.1f ms "
				"95th percentile (%.2f and %.2f refreshes), %u frames", median, high,
				median * presentation.refresh_rate / 1000.0f, high * presentation.refresh_rate / 1000.0f, frames);
	}
	if (presentation.unpaced)
		host_logf(HOST_LOG_INFO, "metal: vsync off: the newest frame taken %.1f ms before the link's deadline, at most %.2f ms late",
			presentation.take_margin * 1000.0, atomic_exchange(&presentation.take_late_most_us, 0) / 1000.0);
	else
	{
		/* (as Vulkan's pacing line, xgpu_vulkan_pacing.c) */
		struct host_render_pacing_statistics pacing;

		host_render_stream_pacing_statistics(&pacing);
		host_logf(HOST_LOG_INFO, "metal: pacing on: %u refreshes showed a frame again; %u frames taken at their refresh, "
			"%u after it (%u ran long), %u started late; margin %.2f ms, budget %.2f ms; taken %.1f ms before the "
			"link's deadline, at most %.2f ms late", presentation.skipped, pacing.kept,
			pacing.missed, pacing.overran, pacing.late_starts, (double)pacing.margin / 1e6, (double)pacing.budget / 1e6,
			presentation.take_margin * 1000.0, atomic_exchange(&presentation.take_late_most_us, 0) / 1000.0);
	}
	host_logf(HOST_LOG_INFO,
		"metal: %.1f s: %u refreshes (%.1f Hz), %u presented (%u frames, %u cleared), %u on screen (%.1f a second), "
		"intervals %.2f ms mean, %.2f min, %.2f max, %u late, %u hitches over 33 ms; %u refreshes without a new frame (the GPU %u, an image %u, "
		"playing %u, the game %u), %u callbacks late (drawables all pending %u, some free %u; after another %u, after a slow one %u, target skipped %u, delivered late %u; the slowest %.2f ms), %u waits for an image, "
		"%u frames dropped, %u shown after their refresh; "
		"%u ended on the GPU within 1 ms of their target (the least slack %.2f ms); "
		"drawable %ux%u, presentation's GPU %.3f ms a refresh, queued %.3f ms (the most %.3f); frames %u, draws %u, passes %u; "
		"thermal state %s",
		seconds, presentation.ticks, presentation.ticks / seconds, presentation.frames_presented + presentation.idle_presented,
		presentation.frames_presented, presentation.idle_presented, shown, shown / seconds,
		intervals ? (double)sum / 1000.0 / intervals : 0.0,
		atomic_exchange(&presentation.interval_minimum_us, 0) / 1000.0,
		atomic_exchange(&presentation.interval_maximum_us, 0) / 1000.0, atomic_exchange(&presentation.late, 0),
		atomic_exchange(&presentation.hitches, 0),
		presentation.skipped, presentation.skipped_gpu, presentation.skipped_image, presentation.skipped_playing,
		presentation.skipped_game, presentation.gaps_starved + presentation.gaps_free, presentation.gaps_starved,
		presentation.gaps_free, presentation.gaps_following, presentation.gaps_after_slow, presentation.gaps_target_skipped,
		presentation.gaps_delivered_late,
		presentation.callback_most_seconds * 1000.0, presentation.waits, presentation.dropped, atomic_exchange(&presentation.missed, 0),
		atomic_exchange(&presentation.present_gpu_late, 0), atomic_exchange(&presentation.present_slack_least_us, 1000000) / 1000.0,
		presentation.drawable_width, presentation.drawable_height,
		buffers ? atomic_exchange(&presentation.present_gpu_ns, 0) / 1e6 / buffers : 0.0,
		buffers ? atomic_exchange(&presentation.present_queued_ns, 0) / 1e6 / buffers : 0.0,
		atomic_exchange(&presentation.present_queued_most_ns, 0) / 1e6,
		statistics.frames, statistics.draws, statistics.passes, thermal_state());
	{
		/* (the write tracking's faults: each a trip through the kernel) */
		static uint64_t faults_before;
		uint64_t faults = host_memory_watch_faults();

		host_logf(HOST_LOG_INFO, "metal: %.1f s: %llu write-tracking faults (%.1f a frame)", seconds,
			(unsigned long long)(faults - faults_before),
			presentation.frames_presented ? (double)(faults - faults_before) / presentation.frames_presented : 0.0);
		faults_before = faults;
	}
	if (statistics.frames)
		host_logf(HOST_LOG_INFO,
			"metal: a frame: GPU busy %.2f ms (%.0f%% of the time); its command buffers' GPU time %.2f ms (%u), render thread CPU %.2f ms, %.1f draws, %.1f passes (%.1f put aside, %.1f targets renamed for it), "
			"%.1f ordered uploads, %.1f copies of a target drawn into, %.1f pictures restored; the last played in %.2f ms, its stream %u KB "
			"(the most %u KB); the game waited %u times (%.1f ms); textures made %u, made again %u, images %u, "
			"%.1f MB read; shaders compiled %u (%.1f ms), %u from the library, %u the combiner's specialised; pipelines made %u (%u from the archive, %.1f ms); %u pipelines made while drawing (not prepared); thermal state %s",
			statistics.gpu_busy_seconds * 1000.0 / statistics.frames, statistics.gpu_busy_seconds * 100.0 / seconds,
			statistics.gpu_seconds * 1000.0 / statistics.frames, statistics.command_buffers,
			statistics.render_cpu_seconds * 1000.0 / statistics.frames, (double)statistics.draws / statistics.frames,
			(double)statistics.passes / statistics.frames, (double)statistics.passes_aside / statistics.frames,
			(double)statistics.targets_renamed / statistics.frames, (double)statistics.uploads_ordered / statistics.frames,
			(double)statistics.feedback_copies / statistics.frames,
			(double)statistics.pictures_restored / statistics.frames, ring ? ring->decode_nanoseconds / 1e6 : 0.0,
			ring ? ring->frame_bytes / 1024 : 0, ring ? ring->frame_bytes_most / 1024 : 0, waits - waits_before,
			(wait_nanoseconds - wait_nanoseconds_before) / 1e6, textures.made, textures.remade, textures.images,
			textures.bytes / 1048576.0, statistics.shaders_made, statistics.shader_seconds * 1000.0,
			statistics.shaders_from_library, statistics.shaders_specialised,
			statistics.pipelines_made, statistics.pipelines_from_archive,
			statistics.pipeline_seconds * 1000.0, statistics.draws_waiting, thermal_state());
	waits_before = waits;
	wait_nanoseconds_before = wait_nanoseconds;
	presentation.stats_since = now;
	presentation.ticks = presentation.frames_presented = presentation.idle_presented = 0;
	presentation.skipped = presentation.waits = presentation.dropped = 0;
	presentation.gaps_starved = presentation.gaps_free = presentation.gaps_following = presentation.gaps_after_slow = 0;
	presentation.gaps_target_skipped = presentation.gaps_delivered_late = 0;
	presentation.callback_most_seconds = 0.0;
	presentation.skipped_gpu = presentation.skipped_image = presentation.skipped_playing = presentation.skipped_game = 0;
}

/* ---------- each refresh */

static CAFrameRateRange link_rate_range(int saving);

/* (vsync off) the time to give a frame before the take that shows it:
the most the last FRAME_PACING_COSTS frames took from their input read to
queued for the display, which the take can show (not frame_pacing.h's
cost, which leaves out the frame's waits for the render thread), and
FRAME_PACING_MARGIN_NS; but less than a refresh (period), less a
millisecond: the frame for the next take would else start before this
one, and could be ready for it too, and be shown instead */
static uint64_t take_budget(uint64_t period)
{
	uint64_t most = 0;
	uint32_t index;

	for (index = 0; index < FRAME_PACING_COSTS; index++)
	{
		uint64_t each = (uint64_t)atomic_load(&presentation.take_costs_us[index]) * 1000u;

		if (each > most)
			most = each;
	}
	most += FRAME_PACING_MARGIN_NS;
	return period > 2000000u && most > period - 1000000u ? period - 1000000u : most;
}

/* (the link's thread, unpaced) until a time of CACurrentMediaTime. An
ordinary timed wait ends up to 10 ms late here (macOS coalesces timers:
measured on an M2), so the thread is a time constraint thread while it
waits, as the game's is (host_render_stream.c's sleep_until) */
static void take_wait(double when)
{
	static mach_timebase_info_data_t timebase;
	thread_port_t thread = pthread_mach_thread_np(pthread_self());
	thread_time_constraint_policy_data_t constraint;
	thread_standard_policy_data_t standard;
	uint64_t late;

	if (!timebase.denom)
		mach_timebase_info(&timebase);
	/* (a refresh's period at 120 Hz, a little of it computing, woken
	within a millisecond: in the time base's units) */
	constraint.period = (uint32_t)(8333333ull * timebase.denom / timebase.numer);
	constraint.computation = (uint32_t)(500000ull * timebase.denom / timebase.numer);
	constraint.constraint = (uint32_t)(1000000ull * timebase.denom / timebase.numer);
	constraint.preemptible = 1;
	thread_policy_set(thread, THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&constraint,
		THREAD_TIME_CONSTRAINT_POLICY_COUNT);
	mach_wait_until((uint64_t)(when * 1e9 * timebase.denom / timebase.numer));
	memset(&standard, 0, sizeof(standard));
	thread_policy_set(thread, THREAD_STANDARD_POLICY, (thread_policy_t)&standard, THREAD_STANDARD_POLICY_COUNT);
	late = (uint64_t)((seconds_now() - when) * 1e6);
	if (late > atomic_load(&presentation.take_late_most_us))
		atomic_store(&presentation.take_late_most_us, late);
}

@interface HaloDisplayLinkTarget : NSObject <CAMetalDisplayLinkDelegate>
@end

/* ---------- an iPhone's rate: 120 while the frames keep up, else 60

A frame that misses its refresh at 120 Hz shows the one before for two
refreshes: in a busy fight, a phone's GPU or its heat makes some of the
frames late, and the picture judders between 8 and 17 ms steps. Half the
rate shows every frame for two refreshes, evenly. Each second of play, the
refreshes that showed no new frame (the game's own, not a loading screen)
are counted: more than RATE_ADAPT_REPEATS of them at the full rate halves
it; at half, the full rate is tried again after a hold that starts at
RATE_ADAPT_HOLD seconds and doubles (to RATE_ADAPT_HOLD_MOST) at each
try that fails, and starts again once the full rate has held for
RATE_ADAPT_GOOD seconds. Battery saver's and HALO_METAL_RATE's rates are
their own; HALO_METAL_ADAPTIVE_RATE=0: the full rate always. */

#define RATE_ADAPT_REPEATS 0.10
#define RATE_ADAPT_HOLD 5.0
#define RATE_ADAPT_HOLD_MOST 80.0
#define RATE_ADAPT_GOOD 10

/* a refresh of the game's frames (shown: a new frame); whether the link's
rate is to change */
static BOOL rate_adapt(BOOL shown, double now)
{
#if TARGET_OS_IPHONE
	static int enabled = -1;
	double share;

	if (enabled < 0)
		enabled = !getenv("HALO_METAL_RATE") && getenv("HALO_METAL_ADAPTIVE_RATE") &&
			atoi(getenv("HALO_METAL_ADAPTIVE_RATE")) != 0;
	if (!enabled || presentation.battery_saver_set || presentation.refresh_rate < 90.0f)
		return NO;
	if (presentation.adapt_since == 0.0)
		presentation.adapt_since = now;
	presentation.adapt_refreshes++;
	if (!shown)
		presentation.adapt_repeats++;
	if (now - presentation.adapt_since < 1.0)
		return NO;
	share = (double)presentation.adapt_repeats / (double)presentation.adapt_refreshes;
	presentation.adapt_refreshes = presentation.adapt_repeats = 0;
	presentation.adapt_since = now;
	if (!presentation.rate_halved)
	{
		if (share <= RATE_ADAPT_REPEATS)
		{
			if (++presentation.adapt_good_windows >= RATE_ADAPT_GOOD)
				presentation.adapt_hold = 0.0;
			return NO;
		}
		presentation.adapt_good_windows = 0;
		presentation.adapt_hold = presentation.adapt_hold > 0.0 ?
			fmin(presentation.adapt_hold * 2.0, RATE_ADAPT_HOLD_MOST) : RATE_ADAPT_HOLD;
		presentation.adapt_hold_until = now + presentation.adapt_hold;
		presentation.rate_halved = 1;
		host_logf(HOST_LOG_INFO, "metal: %.0f%% of a second's refreshes showed no new frame: the display link at %.0f Hz "
			"(the full rate tried again in %.0f s)", share * 100.0, presentation.refresh_rate / 2.0f, presentation.adapt_hold);
		return YES;
	}
	if (now < presentation.adapt_hold_until)
		return NO;
	presentation.rate_halved = 0;
	presentation.adapt_good_windows = 0;
	host_logf(HOST_LOG_INFO, "metal: the display link at %.0f Hz again", presentation.refresh_rate);
	return YES;
#else
	(void)shown;
	(void)now;
	return NO;
#endif
}

@implementation HaloDisplayLinkTarget

- (void)metalDisplayLink:(CAMetalDisplayLink *)link needsUpdate:(CAMetalDisplayLinkUpdate *)update
{
	double start = seconds_now();

	if (atomic_load(&presentation.background))
		return;
	[self refresh:update];
	presentation.last_callback_seconds = seconds_now() - start;
	if (presentation.last_callback_seconds > presentation.callback_most_seconds)
		presentation.callback_most_seconds = presentation.last_callback_seconds;
	(void)link;
}

- (void)refresh:(CAMetalDisplayLinkUpdate *)update
{
	CAMetalDisplayLink *link = presentation.link;

	/* (battery saver turned on or off, or the window on a display of
	another rate: the link's rate, from its next refresh, and its period
	measured again) */
	if (atomic_load(&presentation.battery_saver) != presentation.battery_saver_set ||
		atomic_exchange(&presentation.rate_changed, 0))
	{
		presentation.battery_saver_set = atomic_load(&presentation.battery_saver);
		link.preferredFrameRateRange = link_rate_range(presentation.battery_saver_set);
		memset(presentation.target_intervals, 0, sizeof(presentation.target_intervals));
	}

	@autoreleasepool
	{
		id<CAMetalDrawable> drawable = update.drawable;
		uint32_t width = atomic_load(&presentation.pixel_width), height = atomic_load(&presentation.pixel_height);
		MTLRenderPassDescriptor *pass;
		id<MTLCommandBuffer> buffer;
		id<MTLRenderCommandEncoder> encoder;
		id<MTLTexture> image = nil;
		id<MTLTexture> loading_image = nil, sampled_image;
		double now = seconds_now(), target = update.targetPresentationTimestamp, committed;
		int index = -1, discarded = 0;
		BOOL idle;
		struct xgpu_metal_compile_totals compiled = { 0 };
		uint64_t input = 0, producer_frame = 0;
		/* (paced) the take's time and the refreshes' period, on the game's
		clock */
		uint64_t latch_ns = 0, period_ns = 0;
		struct loading_presentation_frame loading_frame;

		(void)link;
		presentation.ticks++;
		/* (display.hdr: the screen's headroom looked at four times a second,
		on the main thread, NSScreen's) */
		if (presentation.edr_ticks++ % 15 == 0)
			dispatch_async(dispatch_get_main_queue(), ^{ display_hdr_read(); });
		layer_hdr_choose();
		if (presentation.last_tick > 0.0 && presentation.refresh_rate > 0.0f &&
			now - presentation.last_tick > 1.5 / presentation.refresh_rate)
		{
			if (atomic_load(&presentation.drawables_pending) >= (int)presentation.layer.maximumDrawableCount - 1)
				presentation.gaps_starved++;
			else
				presentation.gaps_free++;
			if (presentation.last_gap)
				presentation.gaps_following++;
			if (presentation.last_callback_seconds > 1.0 / presentation.refresh_rate)
				presentation.gaps_after_slow++;
			if (update.targetPresentationTimestamp - presentation.last_target > 1.5 / presentation.refresh_rate)
				presentation.gaps_target_skipped++;
			else
				presentation.gaps_delivered_late++;
			presentation.last_gap = YES;
		}
		else
		{
			presentation.last_gap = NO;
		}
		presentation.last_tick = now;
		if (presentation.last_target > 0.0 && update.targetPresentationTimestamp > presentation.last_target &&
			update.targetPresentationTimestamp - presentation.last_target < 0.1)
			presentation.target_intervals[presentation.target_interval_next++ % 32] =
				update.targetPresentationTimestamp - presentation.last_target;
		presentation.last_target = update.targetPresentationTimestamp;
		if (presentation.stats_interval > 0.0 && now - presentation.stats_since >= presentation.stats_interval)
			pacing_log(now);
		/* the frame is latched now (the link calls about two refreshes before
		the frame is on screen; one taken later, nearer the link's deadline,
		misses its refresh): the game starts each frame to be ready by the
		next latch (frame_pacing.h) */
		{
			struct timespec monotonic;
			double offset, period = 0.0;
			uint32_t interval;
			/* (vsync off) the newest frame is taken TAKE_MARGIN_MS
			before the link's deadline for this refresh, not as the link
			calls, a refresh before it: the frame on screen is that much
			newer */
			double take = update.targetTimestamp - presentation.take_margin;

			for (interval = 0; interval < 32; interval++)
			{
				double each = presentation.target_intervals[interval];

				if (each > 0.0 && (period == 0.0 || each < period))
					period = each;
			}
			if (period == 0.0)
				period = 1.0 / presentation.refresh_rate;
			clock_gettime(CLOCK_MONOTONIC, &monotonic);
			offset = (double)monotonic.tv_sec + monotonic.tv_nsec / 1e9 - seconds_now();
			if (!presentation.unpaced)
			{
				/* (the refresh's latch is the link's deadline, its frame's
				work to be done by it: its GPU's too, which the presentation
				waits for. The frame is taken a little before, below) */
				if (presentation.frames_seen && take > now && take - now < 0.1)
					take_wait(take);
				latch_ns = (uint64_t)((update.targetTimestamp + offset) * 1e9);
				period_ns = (uint64_t)(period * 1e9);
			}
			else
			{
				/* (the game's frames start at fixed phases of these times:
				host_metal_frame_phase) */
				uint64_t budget = take_budget((uint64_t)(period * 1e9));

				/* (the budget follows the frames' costs by
				UNPACED_PHASE_SLEW_NS a refresh at most: a jump in the
				frames' start would be a jump in the game's time on
				screen) */
				if (!presentation.take_budget_ns)
					presentation.take_budget_ns = budget;
				else if (budget > presentation.take_budget_ns + UNPACED_PHASE_SLEW_NS)
					presentation.take_budget_ns += UNPACED_PHASE_SLEW_NS;
				else if (budget + UNPACED_PHASE_SLEW_NS < presentation.take_budget_ns)
					presentation.take_budget_ns -= UNPACED_PHASE_SLEW_NS;
				else
					presentation.take_budget_ns = budget;
				atomic_store(&presentation.take_period_ns, (uint64_t)(period * 1e9));
				atomic_store(&presentation.take_budget_published_ns, presentation.take_budget_ns);
				atomic_store(&presentation.take_phase_ns, (uint64_t)((take + offset) * 1e9) - presentation.take_budget_ns);
				if (presentation.frames_seen && take > now && take - now < 0.1)
					take_wait(take);
			}
		}
		/* (a new size is the next drawable's) */
		if (width && height && (width != presentation.drawable_width || height != presentation.drawable_height))
		{
			presentation.layer.drawableSize = CGSizeMake(width, height);
			presentation.drawable_width = width;
			presentation.drawable_height = height;
		}
		pthread_mutex_lock(&presentation.lock);
		loading_frame = presentation.loading;
		if (loading_frame.ready && presentation.queued)
		{
			loading_frame.active = 0;
			presentation.loading.active = 0;
		}
		if (loading_frame.active)
		{
			struct timespec monotonic;

			clock_gettime(CLOCK_MONOTONIC, &monotonic);
			loading_frame.elapsed_ns = (uint64_t)monotonic.tv_sec * 1000000000ull + monotonic.tv_nsec -
				loading_frame.started_ns;
			loading_frame.serial = ++presentation.loading.serial;
			loading_presentation_build(&loading_frame);
		}
		/* Progress pictures are not sampled by the owned loading scene. Their
		producer work was committed before queueing; later reuse stays on that
		same producer queue, so it needs no loading-GPU completion dependency. */
		if (loading_frame.active)
		{
			while (presentation.queued)
			{
				presentation.image_states[presentation.queue[--presentation.queued]] = _image_free;
				discarded++;
			}
			pthread_cond_broadcast(&presentation.freed);
		}
		/* (vsync off: the newest frame, those queued before it dropped) */
		while (presentation.unpaced && presentation.queued > 1)
		{
			presentation.image_states[presentation.queue[0]] = _image_free;
			memmove(presentation.queue, presentation.queue + 1, sizeof(int) * (size_t)(presentation.queued - 1));
			presentation.queued--;
			presentation.dropped++;
			discarded++;
			pthread_cond_broadcast(&presentation.freed);
		}
		if (presentation.queued)
		{
			index = presentation.queue[0];
			memmove(presentation.queue, presentation.queue + 1, sizeof(int) * (size_t)(presentation.queued - 1));
			presentation.queued--;
			presentation.image_states[index] = _image_showing;
			image = presentation.images[index];
			compiled = presentation.compiled[index];
			input = presentation.frame_input[index];
			producer_frame = presentation.frame_ready[index];
		}
		idle = !presentation.frames_seen;
		pthread_mutex_unlock(&presentation.lock);
		while (discarded-- > 0)
			host_render_stream_frame_taken();
		/* (paced: the frame taken at the take, which tells the stream whether
		it came in time for the refresh it was started for; or the take
		alone, nothing new queued) */
		if (image && latch_ns)
		{
			uint64_t finished = atomic_load(&presentation.frame_finished_number[producer_frame % 8]) == producer_frame ?
				atomic_load(&presentation.frame_finished_ns[producer_frame % 8]) : 0;

			host_render_stream_frame_displayed(input, finished, latch_ns, 0, period_ns);
		}
		else if (image)
			host_render_stream_frame_taken();
		else if (latch_ns)
			host_render_stream_display_latched(latch_ns, period_ns);
		if (!idle && !loading_frame.active && rate_adapt(image != nil, now))
		{
			link.preferredFrameRateRange = link_rate_range(presentation.battery_saver_set);
			memset(presentation.target_intervals, 0, sizeof(presentation.target_intervals));
		}
		if (!image && !idle && !loading_frame.active)
		{
			/* (the frame shown stays: nothing new to present) */
			presentation.skipped++;
			if (xgpu_metal_frame_waiting())
				presentation.skipped_gpu++;
			else if (atomic_load(&presentation.image_waiting))
				presentation.skipped_image++;
			else if (host_render_stream_playing())
				presentation.skipped_playing++;
			else
				presentation.skipped_game++;
			return;
		}

		buffer = host_metal_present_buffer(image ? producer_frame : 0);
		if (loading_frame.active)
			loading_image = loading_upload(buffer, &loading_frame, drawable.texture.width, drawable.texture.height);
		sampled_image = loading_image ? loading_image : image;
		pass = [MTLRenderPassDescriptor renderPassDescriptor];
		pass.colorAttachments[0].texture = drawable.texture;
		pass.colorAttachments[0].loadAction = MTLLoadActionClear;
		/* (a frame of the drawable's own size covers all of it: nothing to
		clear, which a tile-based GPU writes into each tile before the draw
		writes over it) */
		if (!loading_frame.active && image && image.width == drawable.texture.width &&
			image.height == drawable.texture.height)
			pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
		pass.colorAttachments[0].storeAction = MTLStoreActionStore;
		/* (black until the game's first frame: the window opens before the
		game draws) */
		pass.colorAttachments[0].clearColor = loading_frame.active && !loading_image ?
			MTLClearColorMake(edr_value(drawable, LOADING_PRESENTATION_RED), edr_value(drawable, LOADING_PRESENTATION_GREEN),
				edr_value(drawable, LOADING_PRESENTATION_BLUE), 1.0) : MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
		encoder = [buffer renderCommandEncoderWithDescriptor:pass];
		if (loading_image || (image && !loading_frame.active))
		{
			/* letterboxed to the frame's shape (the loading screen's is the
			screen's: loading_art_width) */
			double drawable_width = drawable.texture.width, drawable_height = drawable.texture.height;
			double fit_width = drawable_width, fit_height = drawable_width * sampled_image.height / sampled_image.width;

			if (fit_height > drawable_height)
			{
				fit_height = drawable_height;
				fit_width = drawable_height * sampled_image.width / sampled_image.height;
			}
			/* (within a pixel of the screen's shape: all of it) */
			if (loading_image && fabs(fit_width - drawable_width) < sampled_image.width / 480.0 + 1.0)
				fit_width = drawable_width;
			if (loading_image && fabs(fit_height - drawable_height) < 2.0)
				fit_height = drawable_height;
			MTLViewport viewport = { floor((drawable_width - fit_width) / 2.0), floor((drawable_height - fit_height) / 2.0),
				floor(fit_width), floor(fit_height), 0.0, 1.0 };

			[encoder setViewport:viewport];
			[encoder setFragmentTexture:sampled_image atIndex:0];
			[encoder setFragmentSamplerState:presentation.present_sampler atIndex:0];
			if (drawable.texture.pixelFormat == MTLPixelFormatRGBA16Float && presentation.edr_pipeline)
			{
				/* display.hdr: the frame encoded for the EDR layer (xgpu_hdr.c) */
				struct xgpu_metal_post_uniforms uniforms;
				struct xgpu_hdr_frame frame;

				host_metal_hdr_frame(&frame);
				memset(&uniforms, 0, sizeof(uniforms));
				uniforms.metrics[0] = (float)viewport.originX;
				uniforms.metrics[1] = (float)viewport.originY;
				uniforms.metrics[2] = 1.0f / (float)viewport.width;
				uniforms.metrics[3] = 1.0f / (float)viewport.height;
				uniforms.hdr[0] = frame.output == XGPU_HDR_OUTPUT_SDR ? 1.0f : frame.paper_white;
				uniforms.hdr[1] = frame.output == XGPU_HDR_OUTPUT_SDR ? 1.0f : frame.peak;
				uniforms.hdr[2] = (float)XGPU_HDR_OUTPUT_EXTENDED_LINEAR;
				uniforms.hdr[3] = frame.output == XGPU_HDR_OUTPUT_SDR ? 1.0f : frame.ui;
				[encoder setRenderPipelineState:presentation.edr_pipeline];
				[encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:XGPU_METAL_BUFFER_DRAW_UNIFORMS];
				[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
			}
			else if (drawable.texture.pixelFormat == MTLPixelFormatBGRA8Unorm && gamma_power() > 0.0f)
			{
				/* display.gamma: the frame curved as it is drawn into the
				SDR layer (no pass of its own: this is the frame's last) */
				struct xgpu_metal_post_uniforms uniforms;

				memset(&uniforms, 0, sizeof(uniforms));
				uniforms.metrics[0] = (float)viewport.originX;
				uniforms.metrics[1] = (float)viewport.originY;
				uniforms.metrics[2] = 1.0f / (float)viewport.width;
				uniforms.metrics[3] = 1.0f / (float)viewport.height;
				uniforms.bounds[0] = gamma_power();
				[encoder setRenderPipelineState:presentation.gamma_pipeline];
				[encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:XGPU_METAL_BUFFER_DRAW_UNIFORMS];
				[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
			}
			else
			{
				[encoder setRenderPipelineState:presentation.present_pipeline];
				[encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
			}
		}
		if (loading_frame.active && !loading_image)
		{
			uint32_t rectangle_index;

			[encoder setRenderPipelineState:drawable.texture.pixelFormat == MTLPixelFormatRGBA16Float &&
				presentation.loading_edr_pipeline ? presentation.loading_edr_pipeline : presentation.loading_pipeline];
			[encoder setViewport:(MTLViewport){0.0, 0.0, drawable.texture.width, drawable.texture.height, 0.0, 1.0}];
			for (rectangle_index = 0; rectangle_index < loading_frame.count; rectangle_index++)
			{
				const struct loading_presentation_rect *rectangle = &loading_frame.rectangles[rectangle_index];
				float parameters[12] = {0};

				parameters[0] = rectangle->x * (2.0f / LOADING_PRESENTATION_WIDTH) - 1.0f;
				parameters[1] = 1.0f - rectangle->y * (2.0f / LOADING_PRESENTATION_HEIGHT);
				parameters[2] = (rectangle->x + rectangle->width) * (2.0f / LOADING_PRESENTATION_WIDTH) - 1.0f;
				parameters[3] = 1.0f - (rectangle->y + rectangle->height) * (2.0f / LOADING_PRESENTATION_HEIGHT);
				memcpy(parameters + 4, rectangle->color, sizeof(rectangle->color));
				parameters[4] = edr_value(drawable, parameters[4]);
				parameters[5] = edr_value(drawable, parameters[5]);
				parameters[6] = edr_value(drawable, parameters[6]);
				[encoder setVertexBytes:parameters length:sizeof(parameters) atIndex:0];
				[encoder setFragmentBytes:parameters length:sizeof(parameters) atIndex:0];
				[encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
			}
		}
		[encoder endEncoding];
		atomic_fetch_add(&presentation.drawables_pending, 1);
#if TARGET_OS_SIMULATOR
		/* (the iOS simulator's drawables say nothing of their presentation:
		taken as shown once drawn, at no known time) */
		[buffer addCompletedHandler:^(id<MTLCommandBuffer> done) {
			(void)done;
			atomic_fetch_sub(&presentation.drawables_pending, 1);
			presented(0.0, target, image ? &compiled : NULL);
		}];
#else
		[drawable addPresentedHandler:^(id<MTLDrawable> shown) {
			atomic_fetch_sub(&presentation.drawables_pending, 1);
			presented(shown.presentedTime, target, image ? &compiled : NULL);
			host_load_trace("metal.presented", "display_time=%.9f target=%.9f image=%d input=%llu",
				shown.presentedTime, target, image != nil, (unsigned long long)input);
			if (loading_frame.active)
				host_load_trace("loading.presented", "epoch=%llu serial=%llu elapsed_ns=%llu display_time=%.9f",
					(unsigned long long)loading_frame.epoch, (unsigned long long)loading_frame.serial,
					(unsigned long long)loading_frame.elapsed_ns, shown.presentedTime);
			if (!loading_frame.active && input && shown.presentedTime > 0.0)
			{
				/* (the display's time on the game's clock) */
				struct timespec monotonic;
				double offset;

				clock_gettime(CLOCK_MONOTONIC, &monotonic);
				offset = (double)monotonic.tv_sec + monotonic.tv_nsec / 1e9 - CACurrentMediaTime();
				host_render_stream_frame_shown(input, (uint64_t)((shown.presentedTime + offset) * 1e9));
			}
		}];
#endif
		[buffer presentDrawable:drawable];
		committed = seconds_now();
		[buffer addCompletedHandler:^(id<MTLCommandBuffer> done) {
			if (done.status == MTLCommandBufferStatusError)
				host_logf(HOST_LOG_ERROR, "metal: a presentation command buffer failed: %s",
					done.error.localizedDescription.UTF8String);
			if (done.GPUEndTime > done.GPUStartTime)
			{
				uint64_t queued = done.GPUStartTime > committed ? (uint64_t)((done.GPUStartTime - committed) * 1e9) : 0;

				xgpu_metal_gpu_interval(done.GPUStartTime, done.GPUEndTime);
				atomic_fetch_add(&presentation.present_gpu_ns, (uint64_t)((done.GPUEndTime - done.GPUStartTime) * 1e9));
				atomic_fetch_add(&presentation.present_queued_ns, queued);
				if (queued > atomic_load(&presentation.present_queued_most_ns))
					atomic_store(&presentation.present_queued_most_ns, queued);
				atomic_fetch_add(&presentation.present_buffers, 1);
				{
					int64_t slack = (int64_t)((target - done.GPUEndTime) * 1e6);

					if (slack < 1000)
						atomic_fetch_add(&presentation.present_gpu_late, 1);
					if (slack < atomic_load(&presentation.present_slack_least_us))
						atomic_store(&presentation.present_slack_least_us, slack);
				}
			}
		}];
		if (index >= 0)
		{
			[buffer addCompletedHandler:^(id<MTLCommandBuffer> done) {
				(void)done;
				image_release(index);
			}];
			presentation.frames_presented++;
		}
		else
		{
			presentation.idle_presented++;
		}
		[buffer commit];
	}
}

@end

/* the link's rate: a ProMotion display's follows the frames, up to its
highest; HALO_METAL_RATE, the link at a rate of its own (the pacing of
another display, tested on this one); battery saver (saving), a whole
fraction of the display's, 30 to 60 frames a second (frame_pacing.h). An
iPhone's is its highest alone: given a range, iOS chose 80 Hz on an iPhone
15 Pro Max that held 120 with its GPU half idle */
static CAFrameRateRange link_rate_range(int saving)
{
	float rate = presentation.refresh_rate > 0.0f ? presentation.refresh_rate : 60.0f;

#if TARGET_OS_IPHONE
	if (presentation.rate_halved && !saving && !getenv("HALO_METAL_RATE"))
	{
		rate /= 2.0f;
		return CAFrameRateRangeMake(rate, rate, rate);
	}
#endif

	if (getenv("HALO_METAL_RATE"))
		rate = (float)atof(getenv("HALO_METAL_RATE"));
	if (saving)
	{
		rate /= (float)frame_pacing_divisor(rate, BATTERY_SAVER_LEAST, BATTERY_SAVER_MOST);
		host_logf(HOST_LOG_INFO, "metal: battery saver: the display link at %.0f Hz", rate);
		return CAFrameRateRangeMake(rate, rate, rate);
	}
#if TARGET_OS_IPHONE
	/* (its highest alone: given a range, iOS slows the link to save power,
	to 80 Hz on an iPhone 15 Pro Max whose frames kept 120; rate_adapt's
	halving is off by default, its changes pausing the link) */
	return CAFrameRateRangeMake(rate, rate, rate);
#else
	return getenv("HALO_METAL_RATE") ? CAFrameRateRangeMake(rate, rate, rate) :
		CAFrameRateRangeMake(rate < 60.0f ? rate : 30.0f, rate, rate);
#endif
}

static void *presentation_thread(void *context)
{
	HaloDisplayLinkTarget *target = (__bridge_transfer HaloDisplayLinkTarget *)context;

	pthread_setname_np("metal presentation");
	@autoreleasepool
	{
		CAMetalDisplayLink *link = [[CAMetalDisplayLink alloc] initWithMetalLayer:presentation.layer];

		link.delegate = target;
		presentation.battery_saver_set = atomic_load(&presentation.battery_saver);
		link.preferredFrameRateRange = link_rate_range(presentation.battery_saver_set);
		link.preferredFrameLatency = 1.0f;
		presentation.link = link;
		[link addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
	}
	for (;;)
	{
		@autoreleasepool
		{
			[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate distantFuture]];
		}
	}
	return NULL;
}

/* (the main thread, as SDL reports the window's events) */
static bool SDLCALL window_event(void *userdata, SDL_Event *event)
{
	(void)userdata;
	if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED &&
		SDL_GetWindowFromID(event->window.windowID) == presentation.window)
	{
		int width = event->window.data1, height = event->window.data2;

		drawn_size(&width, &height);
		atomic_store(&presentation.pixel_width, (uint32_t)width);
		atomic_store(&presentation.pixel_height, (uint32_t)height);
	}
	return true;
}

#if !TARGET_OS_IPHONE
/* (the main thread, as the window moves to another screen) the screen's
highest rate: the link's (link_rate_range), from its next refresh. The
guest's frames follow SDL's rate for the window's display by themselves
(sdl_platform.c's frame_interval_ns). */
static void display_rate_read(void)
{
	NSWindow *native = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(presentation.window),
		SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
	NSScreen *screen = native.screen;
	float rate = screen ? (float)screen.maximumFramesPerSecond : 0.0f;

	if (rate <= 0.0f || rate == presentation.refresh_rate)
		return;
	host_logf(HOST_LOG_INFO, "metal: the window is on %s, at up to %.0f Hz%s", screen.localizedName.UTF8String, rate,
		screen.minimumRefreshInterval < screen.maximumRefreshInterval ? " (variable)" : "");
	presentation.refresh_rate = rate;
	atomic_store(&presentation.rate_changed, 1);
}
#endif

/* the shaders built ahead of time (tools/macos_build.py): the app's
resource, or the file beside a bare executable (HALO_METAL_LIBRARY: another,
"" none) */
static const char *shader_library_path(void)
{
	static char path[1024];
	const char *chosen = getenv("HALO_METAL_LIBRARY");
	NSString *found;

	if (chosen)
		return *chosen ? chosen : NULL;
	found = [[NSBundle mainBundle] pathForResource:@"shaders" ofType:@"metallib"];
	if (!found)
		return NULL;
	snprintf(path, sizeof(path), "%s", found.fileSystemRepresentation);
	return path;
}

/* the backend, set up for this process (once; host_main.c, before the
guest starts, so that the command stream finds it) */
int host_metal_initialize(void)
{
	static int result = -1;
	const char *edr = getenv("HALO_METAL_EDR_HEADROOM");
	struct xgpu_metal_options options;
	char path[1024];
	const char *archive = getenv("HALO_METAL_ARCHIVE");
	const char *home = getenv("HOME");

	if (result >= 0)
		return result;
	atomic_store(&presentation.edr_potential, float_bits(1.0f));
	atomic_store(&presentation.edr_current, float_bits(1.0f));
	atomic_store(&presentation.hdr_paper_white_nits, 0);
	atomic_store(&presentation.hdr_paper_white, 100);
	atomic_store(&presentation.hdr_ui_nits, 0);
	if (edr && atof(edr) > 0.0)
		host_metal_hdr_override((float)atof(edr));
	memset(&options, 0, sizeof(options));
	if (!archive && home)
	{
		snprintf(path, sizeof(path), "%s/Library/Caches/OpenH1/metal_pipelines.bin", home);
		archive = path;
	}
	options.archive_path = archive && *archive ? archive : NULL;
#if TARGET_OS_SIMULATOR
	/* (the iOS simulator's GPU has no binary archives: Metal stops the
	process at the first pipeline made with one) */
	options.archive_path = NULL;
#endif
	options.library_path = shader_library_path();
	options.page_size = (uint32_t)getpagesize();
	/* (the 3D view in one pass on iOS, whose GPUs pay the most for a pass's
	stores and loads: xgpu_metal.h; HALO_METAL_ASIDE_BREAK=1 or 0 chooses) */
#if TARGET_OS_IPHONE
	options.aside_continue = 1;
#endif
	if (getenv("HALO_METAL_ASIDE_BREAK") && *getenv("HALO_METAL_ASIDE_BREAK"))
		options.aside_continue = !atoi(getenv("HALO_METAL_ASIDE_BREAK"));
	options.guest_extent = host_memory_readable_extent;
	result = xgpu_metal_initialize(&options);
	if (result)
	{
		host_memory_remapped = xgpu_metal_guest_remapped;
		host_render_stream_set_backend(xgpu_metal_stream_backend());
		xgpu_metal_stream_set_shaders(xgpu_metal_shaders());
		host_render_record_set_shooter(xgpu_metal_stream_shot);
		host_render_stream_set_screenshot_writer(host_screenshot_write);
	}
	return result;
}

/* (a host import: the guest's sdl_platform.c, as battery saver turns on or
off) the link's rate, set on its thread at its next refresh */
void host_metal_battery_saver(int on)
{
	atomic_store(&presentation.battery_saver, on ? 1 : 0);
}

/* (a host import: the guest's sdl_platform.c, as the window opens and as
display.vsync changes) on, the game is paced by the link's refreshes; off,
it draws as fast as its own clock lets it (display.max_fps, sdl_platform.c's
platform_video_swap) and each refresh shows the newest frame, those queued
before it dropped (as HALO_METAL_UNPACED): no tearing, the least latency */
void host_metal_vsync(int on)
{
	BOOL unpaced = !on || presentation.unpaced_forced;

	pthread_mutex_lock(&presentation.lock);
	presentation.unpaced = unpaced;
	pthread_cond_broadcast(&presentation.freed);
	pthread_mutex_unlock(&presentation.lock);
	host_render_stream_display_paced(!unpaced);
}

/* (host imports: the guest's sdl_platform.c, before each frame with vsync
off) the game starts its frames at fixed phases of the display's
refreshes, so that the newest frame each refresh takes is always as old:
one starts its budget (the most a frame has taken from its input read to
queued, take_budget) before a refresh's take, the others (two frames a
refresh at display.max_fps's twice the refresh rate) whole steps after it
but none before the take (sdl_platform.c's platform_frame_slot). Frames
on a clock of their own
beat against the display's: the frame shown was then one just drawn or one
an interval older, and the game's time on screen stepped unevenly (on an
M2 at 60 Hz, up to 20% of the refreshes by more than 4 ms). The phase is
a start time on the game's clock (CLOCK_MONOTONIC nanoseconds), any of
whose refreshes' periods later or earlier is one too; 0 while the display
paces the game, or before its first refresh. */
unsigned long long host_metal_frame_phase(void)
{
	if (!presentation.unpaced || !atomic_load(&presentation.take_period_ns))
		return 0;
	return atomic_load(&presentation.take_phase_ns);
}

/* the refreshes' period in nanoseconds (0: not known yet) */
unsigned int host_metal_frame_period(void)
{
	return (unsigned int)atomic_load(&presentation.take_period_ns);
}

/* the time in nanoseconds from the phase to the refresh's take: the
shown frame's budget; a frame started in it, after the shown one, could be
ready for the take too, and be shown instead (as the frame's cost varies:
uneven again) */
unsigned int host_metal_frame_budget(void)
{
	return (unsigned int)atomic_load(&presentation.take_budget_published_ns);
}

/* until a frame's start (CLOCK_MONOTONIC nanoseconds), on time: the
guest's own timed waits ended up to 8 ms late (an M2), each such frame
missing its refresh's take */
void host_metal_sleep_until(unsigned long long when)
{
	host_render_stream_sleep_until(when);
}

#if TARGET_OS_IPHONE
/* port/ios/touch_controls.m */
void host_touch_overlay_install(void *window);
/* port/ios/host_app.m: display.render_scale */
float host_app_render_scale(void);
#endif

/* the window's pixels the game draws, and the drawables are made: on iOS
display.render_scale of them, which the display scales up */
static void drawn_size(int *width, int *height)
{
#if TARGET_OS_IPHONE
	*width = (int)(*width * host_app_render_scale() + 0.5f) & ~1;
	*height = (int)(*height * host_app_render_scale() + 0.5f) & ~1;
#else
	(void)width;
	(void)height;
#endif
}

/* (iOS: port/ios/host_app.m, as the app goes to the background and comes
back) nothing presented while away */
void host_metal_background(int away)
{
	atomic_store(&presentation.background, away ? 1 : 0);
}

/* (the replay's: port/macos/tools/halo_replay.m) */
void host_metal_set_unpaced(int unpaced)
{
	presentation.unpaced = unpaced ? YES : NO;
}

/* (a host import: the guest's sdl_platform.c, on the main thread) */
int host_metal_window_open(uint32_t window)
{
	SDL_Window *object = host_sdl_handle_get(window, _handle_window);
	id<MTLLibrary> library;
	MTLRenderPipelineDescriptor *descriptor;
	MTLSamplerDescriptor *sampler;
	NSError *error = nil;
	pthread_t thread;
	pthread_attr_t attributes;
	int width = 0, height = 0, created;
	const char *stats = getenv("HALO_METAL_STATS");

	if (!object || presentation.window || !host_metal_initialize())
		return 0;
	presentation.view = SDL_Metal_CreateView(object);
	if (!presentation.view)
	{
		host_logf(HOST_LOG_ERROR, "metal: cannot make the window's view: %s", SDL_GetError());
		return 0;
	}
	presentation.window = object;
	/* (in front: a window started behind others, as one from a terminal
	can be, shows nothing until it is brought forward, and the drawables
	presented meanwhile never reach the screen) */
	SDL_RaiseWindow(object);
#if TARGET_OS_IPHONE
	/* (the touch controls over the game's view: port/ios/touch_controls.m) */
	host_touch_overlay_install(SDL_GetPointerProperty(SDL_GetWindowProperties(object),
		SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL));
#endif
	presentation.layer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(presentation.view);
	presentation.layer.device = xgpu_metal_device();
	presentation.layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
	presentation.layer.framebufferOnly = YES;
	/* (the presentation writes every pixel, alpha 1: the window server
	need not draw what is behind the window, nor blend the window over it) */
	presentation.layer.opaque = YES;
	presentation.layer.maximumDrawableCount = 3;
	SDL_GetWindowSizeInPixels(object, &width, &height);
	drawn_size(&width, &height);
	atomic_store(&presentation.pixel_width, (uint32_t)width);
	atomic_store(&presentation.pixel_height, (uint32_t)height);
	presentation.layer.drawableSize = CGSizeMake(width, height);
	presentation.drawable_width = (uint32_t)width;
	presentation.drawable_height = (uint32_t)height;
	{
#if TARGET_OS_IPHONE
		UIWindow *native = (__bridge UIWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(object),
			SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
		UIScreen *screen = native.windowScene.screen ? native.windowScene.screen : UIScreen.mainScreen;

		presentation.refresh_rate = (float)screen.maximumFramesPerSecond;
		host_logf(HOST_LOG_INFO, "metal: window %dx%d pixels (scale %.1f), display at up to %ld Hz", width, height,
			screen.nativeScale, (long)screen.maximumFramesPerSecond);
#else
		NSWindow *native = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(object),
			SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
		NSScreen *screen = native.screen ? native.screen : NSScreen.mainScreen;

		presentation.refresh_rate = (float)screen.maximumFramesPerSecond;
		host_logf(HOST_LOG_INFO, "metal: window %dx%d pixels (scale %.1f), display %s at up to %ld Hz%s", width, height,
			screen.backingScaleFactor, screen.localizedName.UTF8String, (long)screen.maximumFramesPerSecond,
			screen.minimumRefreshInterval < screen.maximumRefreshInterval ? " (variable)" : "");
#endif
	}

	if (!host_metal_present_initialize())
		return 0;
	library = xgpu_metal_fixed_library();
	descriptor = xgpu_metal_present_descriptor();
	descriptor.vertexFunction = [library newFunctionWithName:@"halo_present_vertex"];
	descriptor.fragmentFunction = [library newFunctionWithName:@"halo_present_fragment"];
	presentation.present_pipeline = library ? [xgpu_metal_device() newRenderPipelineStateWithDescriptor:descriptor
		error:&error] : nil;
	if (!presentation.present_pipeline)
	{
		host_logf(HOST_LOG_ERROR, "metal: the presentation's pipeline: %s", error.localizedDescription.UTF8String);
		return 0;
	}
	descriptor = xgpu_metal_present_descriptor();
	descriptor.vertexFunction = [library newFunctionWithName:@"xgpu_clear_vertex"];
	descriptor.fragmentFunction = [library newFunctionWithName:@"xgpu_clear_fragment"];
	presentation.loading_pipeline = [xgpu_metal_device() newRenderPipelineStateWithDescriptor:descriptor error:&error];
	if (!presentation.loading_pipeline)
	{
		host_logf(HOST_LOG_ERROR, "metal: the loading presentation's pipeline: %s", error.localizedDescription.UTF8String);
		return 0;
	}
	/* (and into an EDR layer: display.hdr) */
	descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
	presentation.loading_edr_pipeline = [xgpu_metal_device() newRenderPipelineStateWithDescriptor:descriptor error:&error];
	/* display.hdr: the presentation into an EDR layer (its encoding, from
	xgpu_hdr.c's GLSL: xgpu_metal_post_shaders.h) */
	descriptor = [MTLRenderPipelineDescriptor new];
	descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
	descriptor.vertexFunction = post_function(XGPU_METAL_POST_VERTEX);
	descriptor.fragmentFunction = post_function(XGPU_METAL_POST_HDR_ENCODE);
	presentation.edr_pipeline = descriptor.vertexFunction && descriptor.fragmentFunction ?
		[xgpu_metal_device() newRenderPipelineStateWithDescriptor:descriptor error:&error] : nil;
	if (!presentation.edr_pipeline)
		host_logf(HOST_LOG_WARN, "metal: no HDR presentation (%s): the window stays SDR",
			error ? error.localizedDescription.UTF8String : "its functions");
	/* display.gamma: the presentation into an SDR layer with its curve
	(tools/vulkan_post_shaders.py's GLSL: xgpu_metal_post_shaders.h) */
	error = nil;
	descriptor = xgpu_metal_present_descriptor();
	descriptor.vertexFunction = post_function(XGPU_METAL_POST_VERTEX);
	descriptor.fragmentFunction = post_function(XGPU_METAL_POST_GAMMA);
	presentation.gamma_pipeline = descriptor.vertexFunction && descriptor.fragmentFunction ?
		[xgpu_metal_device() newRenderPipelineStateWithDescriptor:descriptor error:&error] : nil;
	if (!presentation.gamma_pipeline)
		host_logf(HOST_LOG_WARN, "metal: no display.gamma (%s): the frame is shown as drawn",
			error ? error.localizedDescription.UTF8String : "its functions");
	display_hdr_read();
#if !TARGET_OS_IPHONE
	[[NSNotificationCenter defaultCenter] addObserverForName:NSWindowDidChangeScreenNotification object:nil
		queue:[NSOperationQueue mainQueue] usingBlock:^(NSNotification *note) {
			(void)note;
			display_hdr_read();
			display_rate_read();
		}];
#endif
	sampler = [MTLSamplerDescriptor new];
	sampler.minFilter = MTLSamplerMinMagFilterLinear;
	sampler.magFilter = MTLSamplerMinMagFilterLinear;
	presentation.present_sampler = [xgpu_metal_device() newSamplerStateWithDescriptor:sampler];
	loading_upload_initialize();

	/* (macOS delays the timers of a process it thinks in the background,
	by tens of milliseconds: the game's waits for the display's refreshes
	are each a few milliseconds, frame_pacing.h) */
	presentation.activity = [[NSProcessInfo processInfo]
		beginActivityWithOptions:NSActivityUserInitiated | NSActivityLatencyCritical
		reason:@"The game draws a frame for each of the display's refreshes"];
	SDL_AddEventWatch(window_event, NULL);
	presentation.stats_interval = stats ? atof(stats) : 0.0;
	presentation.take_margin = (getenv("HALO_METAL_TAKE_MARGIN_MS") ? atof(getenv("HALO_METAL_TAKE_MARGIN_MS")) :
		TAKE_MARGIN_MS) / 1000.0;
	presentation.unpaced = presentation.unpaced_forced =
		getenv("HALO_METAL_UNPACED") && atoi(getenv("HALO_METAL_UNPACED"));
	/* (the game starts each frame as the display takes the last: frame_pacing.h) */
	host_render_stream_display_paced(!presentation.unpaced);
	presentation.stats_since = presentation.started = seconds_now();
	atomic_store(&presentation.present_slack_least_us, 1000000);
	atomic_store(&presentation.active, 1);
	/* (the display's deadlines are each refresh's: the scheduler runs it
	first, as it does the main threads of apps in front) */
	pthread_attr_init(&attributes);
	pthread_attr_set_qos_class_np(&attributes, QOS_CLASS_USER_INTERACTIVE, 0);
	created = pthread_create(&thread, &attributes, presentation_thread, (__bridge_retained void *)[HaloDisplayLinkTarget new]);
	pthread_attr_destroy(&attributes);
	if (created != 0)
	{
		atomic_store(&presentation.active, 0);
		host_logf(HOST_LOG_ERROR, "metal: cannot start the presentation thread");
		return 0;
	}
	pthread_detach(thread);
	return 1;
}
